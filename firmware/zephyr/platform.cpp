#include "platform.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/pm/device.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/ring_buffer.h>
#include <hal/nrf_power.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <initializer_list>

namespace {
const device *gpioPorts[] = {DEVICE_DT_GET(DT_NODELABEL(gpio0)),
                             DEVICE_DT_GET(DT_NODELABEL(gpio1))};
const device *radioSpi = DEVICE_DT_GET(DT_NODELABEL(spi0));
const device *displaySpi = DEVICE_DT_GET(DT_NODELABEL(spi1));
const device *gpsUart = DEVICE_DT_GET(DT_NODELABEL(uart0));
const device *usbUart = DEVICE_DT_GET(DT_NODELABEL(cdc_acm));
const device *flashDev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
const device *adcDev = DEVICE_DT_GET(DT_NODELABEL(adc));
USBD_DEVICE_DEFINE(usbDevice, DEVICE_DT_GET(DT_NODELABEL(usbd)), 0x239a, 0x4405);
USBD_DESC_LANG_DEFINE(usbLang);
USBD_DESC_MANUFACTURER_DEFINE(usbManufacturer, "ThinkNode M1 project");
USBD_DESC_PRODUCT_DEFINE(usbProduct, "ThinkNode M1 Zephyr");
USBD_DESC_SERIAL_NUMBER_DEFINE(usbSerial);
USBD_DESC_CONFIG_DEFINE(usbConfigDescription, "CDC UART");
USBD_CONFIGURATION_DEFINE(usbConfiguration, USB_SCD_SELF_POWERED, 50, &usbConfigDescription);
void usbMessage(usbd_context *ctx, const usbd_msg *msg) {
  if (msg->type == USBD_MSG_VBUS_READY)
    usbd_enable(ctx);
  else if (msg->type == USBD_MSG_VBUS_REMOVED)
    usbd_disable(ctx);
}
void usbInit() {
  for (auto *desc : {&usbLang, &usbManufacturer, &usbProduct, &usbSerial})
    if (usbd_add_descriptor(&usbDevice, desc))
      Hw::fatal("USB descriptor");
  if (usbd_add_configuration(&usbDevice, USBD_SPEED_FS, &usbConfiguration) ||
      usbd_register_class(&usbDevice, "cdc_acm_0", USBD_SPEED_FS, 1) ||
      usbd_msg_register_cb(&usbDevice, usbMessage) || usbd_init(&usbDevice))
    Hw::fatal("USB init");
  usbd_device_set_code_triple(&usbDevice, USBD_SPEED_FS, 0xef, 0x02, 0x01);
  // The installed bootloader can consume the cable-insertion event before
  // jumping here. Sample the present level as well as watching future edges.
  if (!usbd_can_detect_vbus(&usbDevice) || nrf_power_usbregstatus_vbusdet_get(NRF_POWER)) {
    int err = usbd_enable(&usbDevice);
    if (err && err != -EALREADY)
      Hw::fatal("USB enable");
  }
}
K_MSGQ_DEFINE(events, sizeof(Hw::Event), 64, 4);
RING_BUF_DECLARE(gpsBytes, 2048);
atomic_t overflow[2]{};
struct k_spinlock connLock;
bt_conn *connection = nullptr;
atomic_t secure = 0, subscribed = 0;
void enqueue(Hw::EventKind kind, Hw::Link link, const void *data = nullptr, size_t size = 0) {
  Hw::Event e{};
  e.kind = kind;
  e.link = link;
  e.size = std::min(size, sizeof(e.data));
  if (size)
    memcpy(e.data, data, e.size);
  if (k_msgq_put(&events, &e, K_NO_WAIT))
    atomic_set(&overflow[unsigned(link)], 1);
}
void uartRx(const device *dev, void *) {
  if (!uart_irq_update(dev))
    return;
  while (uart_irq_rx_ready(dev)) {
    uint8_t bytes[64];
    int n = uart_fifo_read(dev, bytes, sizeof(bytes));
    if (n <= 0)
      break;
    if (dev == gpsUart) {
      if (ring_buf_put(&gpsBytes, bytes, n) != unsigned(n))
        ring_buf_reset(&gpsBytes);
    } else
      enqueue(Hw::EventKind::Data, Hw::Link::Usb, bytes, n);
  }
}
#define NUS_UUID(v) BT_UUID_128_ENCODE(v, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
static bt_uuid_128 serviceUuid = BT_UUID_INIT_128(NUS_UUID(0x6e400001));
static bt_uuid_128 rxUuid = BT_UUID_INIT_128(NUS_UUID(0x6e400002));
static bt_uuid_128 txUuid = BT_UUID_INIT_128(NUS_UUID(0x6e400003));
void ccc(const bt_gatt_attr *, uint16_t value) {
  atomic_set(&subscribed, value == BT_GATT_CCC_NOTIFY);
}
ssize_t receive(bt_conn *conn, const bt_gatt_attr *, const void *data, uint16_t length,
                uint16_t offset, uint8_t flags) {
  if (offset || flags & BT_GATT_WRITE_FLAG_PREPARE)
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  if (bt_conn_get_security(conn) < BT_SECURITY_L3)
    return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION);
  if (length > sizeof(Hw::Event::data))
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  enqueue(Hw::EventKind::Data, Hw::Link::Ble, data, length);
  return length;
}
BT_GATT_SERVICE_DEFINE(
    nus, BT_GATT_PRIMARY_SERVICE(&serviceUuid),
    BT_GATT_CHARACTERISTIC(&txUuid.uuid, BT_GATT_CHRC_NOTIFY, 0, nullptr, nullptr, nullptr),
    BT_GATT_CCC(ccc, BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
    BT_GATT_CHARACTERISTIC(&rxUuid.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_AUTHEN, nullptr, receive, nullptr));
void advertise(k_work *) {
  const bt_data ad[] = {BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
                        BT_DATA(BT_DATA_UUID128_ALL, serviceUuid.val, 16)};
  const bt_data sd[] = {
      BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1)};
  bt_le_adv_param params = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, 1600, 1640, nullptr);
  int err = bt_le_adv_start(&params, ad, 2, sd, 1);
  if (err && err != -EALREADY)
    Hw::fatal("BLE advertising");
}
K_WORK_DEFINE(advertiseWork, advertise);
void connected(bt_conn *conn, uint8_t err) {
  if (err) {
    k_work_submit(&advertiseWork);
    return;
  }
  auto key = k_spin_lock(&connLock);
  connection = bt_conn_ref(conn);
  k_spin_unlock(&connLock, key);
  atomic_clear(&secure);
  atomic_clear(&subscribed);
  if (bt_conn_set_security(conn, BT_SECURITY_L3))
    bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
  const bt_le_conn_param param{24, 72, 4, 500};
  bt_conn_le_param_update(conn, &param);
}
void disconnected(bt_conn *, uint8_t) {
  auto key = k_spin_lock(&connLock);
  bt_conn *old = connection;
  connection = nullptr;
  k_spin_unlock(&connLock, key);
  if (old)
    bt_conn_unref(old);
  atomic_clear(&secure);
  atomic_clear(&subscribed);
  enqueue(Hw::EventKind::Disconnected, Hw::Link::Ble);
  k_work_submit(&advertiseWork);
}
void securityChanged(bt_conn *conn, bt_security_t level, bt_security_err err) {
  if (!err && level >= BT_SECURITY_L3) {
    atomic_set(&secure, 1);
    enqueue(Hw::EventKind::Connected, Hw::Link::Ble);
  } else
    bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}
void keyDisplay(bt_conn *, unsigned key) {
  char s[7];
  snprintf(s, sizeof(s), "%06u", key);
  enqueue(Hw::EventKind::Passkey, Hw::Link::Ble, s, 6);
}
void keyCancel(bt_conn *) {
  enqueue(Hw::EventKind::Disconnected, Hw::Link::Ble);
}
bt_conn_cb connectionCallbacks{};
bt_conn_auth_cb authCallbacks{};
gpio_callback radioCallback{};
void (*radioIsr)() = nullptr;
void gpioInterrupt(const device *, gpio_callback *, gpio_port_pins_t) {
  if (radioIsr)
    radioIsr();
}
} // namespace
namespace Hw {
void init() {
  for (const device *d :
       {gpioPorts[0], gpioPorts[1], radioSpi, displaySpi, gpsUart, usbUart, flashDev, adcDev})
    if (!device_is_ready(d))
      fatal("device not ready");
  uart_irq_callback_user_data_set(gpsUart, uartRx, nullptr);
  uart_irq_callback_user_data_set(usbUart, uartRx, nullptr);
  uart_irq_rx_enable(usbUart);
  usbInit();
  authCallbacks.passkey_display = keyDisplay;
  authCallbacks.cancel = keyCancel;
  if (bt_conn_auth_cb_register(&authCallbacks))
    fatal("BLE auth init");
  connectionCallbacks.connected = connected;
  connectionCallbacks.disconnected = disconnected;
  connectionCallbacks.security_changed = securityChanged;
  bt_conn_cb_register(&connectionCallbacks);
  if (bt_enable(nullptr))
    fatal("BLE init");
  if (settings_load())
    fatal("BLE settings");
  k_work_submit(&advertiseWork);
}
uint32_t now() {
  return k_uptime_get_32();
}
uint32_t micros() {
  return uint32_t(k_ticks_to_us_floor64(k_uptime_ticks()));
}
void sleep(uint32_t ms) {
  k_msleep(ms ? ms : 1);
}
void delayUs(uint32_t us) {
  k_busy_wait(us);
}
void pin(unsigned p, Mode mode) {
  if (p < 48 && gpio_pin_configure(gpioPorts[p / 32], p % 32,
                                   mode == Mode::Output
                                       ? GPIO_OUTPUT
                                       : (GPIO_INPUT | (mode == Mode::PullUp ? GPIO_PULL_UP : 0))))
    fatal("GPIO");
}
void write(unsigned p, bool high) {
  if (p < 48)
    gpio_pin_set_raw(gpioPorts[p / 32], p % 32, high);
}
bool read(unsigned p) {
  return p < 48 && gpio_pin_get_raw(gpioPorts[p / 32], p % 32) > 0;
}
void interrupt(unsigned p, void (*fn)(), bool rising) {
  if (p != RadioIrq)
    fatal("radio IRQ pin");
  radioIsr = fn;
  gpio_init_callback(&radioCallback, gpioInterrupt, BIT(p));
  gpio_add_callback(gpioPorts[0], &radioCallback);
  gpio_pin_interrupt_configure(gpioPorts[0], p,
                               rising ? GPIO_INT_EDGE_RISING : GPIO_INT_EDGE_FALLING);
}
void detach(unsigned p) {
  if (p == RadioIrq) {
    gpio_pin_interrupt_configure(gpioPorts[0], p, GPIO_INT_DISABLE);
    gpio_remove_callback(gpioPorts[0], &radioCallback);
    radioIsr = nullptr;
  }
}
void spi(Bus bus, const uint8_t *tx, uint8_t *rx, size_t count) {
  spi_config config{};
  config.frequency = 4000000;
  config.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
  spi_buf t{const_cast<uint8_t *>(tx), count}, r{rx, count};
  spi_buf_set ts{&t, 1}, rs{&r, 1};
  if (spi_transceive(bus == Bus::Radio ? radioSpi : displaySpi, &config, &ts, rx ? &rs : nullptr))
    fatal("SPI");
}
void gps(bool awake) {
  pin(GpsWake, Mode::Output);
  write(GpsWake, awake);
  if (awake) {
    pm_device_action_run(gpsUart, PM_DEVICE_ACTION_RESUME);
    uart_irq_rx_enable(gpsUart);
  } else {
    uart_irq_rx_disable(gpsUart);
    pm_device_action_run(gpsUart, PM_DEVICE_ACTION_SUSPEND);
    pin(GpsRx, Mode::Input);
    pin(GpsTx, Mode::Input);
    ring_buf_reset(&gpsBytes);
  }
}
int gpsRead() {
  uint8_t byte;
  return ring_buf_get(&gpsBytes, &byte, 1) ? byte : -1;
}
float batteryVolts() {
  adc_channel_cfg cfg{};
  cfg.gain = ADC_GAIN_1_5;
  cfg.reference = ADC_REF_INTERNAL;
  cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40);
  cfg.channel_id = 0;
  cfg.input_positive = NRF_SAADC_AIN2; // Zephyr channel IDs are zero-based; AIN2 = P0.04
  if (adc_channel_setup(adcDev, &cfg))
    return -1;
  int16_t sample = 0;
  adc_sequence seq{};
  seq.channels = 1;
  seq.buffer = &sample;
  seq.buffer_size = sizeof(sample);
  seq.resolution = 12;
  seq.oversampling = 4;
  if (adc_read(adcDev, &seq) || sample <= 0)
    return -1;
  return float(sample) * 3.0f * 2.02f / 4095;
}
bool event(Event &e) {
  for (unsigned i = 0; i < 2; ++i)
    if (atomic_cas(&overflow[i], 1, 0)) {
      k_msgq_purge(&events);
      e = {};
      e.kind = EventKind::Overflow;
      e.link = Link(i);
      return true;
    }
  return k_msgq_get(&events, &e, K_NO_WAIT) == 0;
}
bool usbReady() {
  uint32_t dtr = 0;
  return uart_line_ctrl_get(usbUart, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr;
}
bool bleReady() {
  return atomic_get(&secure) && atomic_get(&subscribed);
}
bool send(Link link, const char *data, size_t size) {
  if (link == Link::Usb) {
    uint32_t dtr = 0;
    uart_line_ctrl_get(usbUart, UART_LINE_CTRL_DTR, &dtr);
    if (!dtr)
      return false;
    for (size_t i = 0; i < size; ++i)
      uart_poll_out(usbUart, data[i]);
    return true;
  }
  if (!bleReady())
    return false;
  auto key = k_spin_lock(&connLock);
  bt_conn *conn = connection ? bt_conn_ref(connection) : nullptr;
  k_spin_unlock(&connLock, key);
  if (!conn)
    return false;
  uint32_t start = now();
  size_t offset = 0;
  while (offset < size && bleReady()) {
    size_t length = std::min(size - offset, size_t(20));
    int err = bt_gatt_notify(conn, &nus.attrs[2], data + offset, length);
    if (!err)
      offset += length;
    else if ((err == -ENOMEM || err == -EAGAIN) && uint32_t(now() - start) < 2000)
      sleep(10);
    else
      break;
  }
  bt_conn_unref(conn);
  return offset == size;
}
bool flashRead(uint32_t a, void *b, size_t n) {
  return a >= 0xE5000 && a + n <= 0xE7000 && flash_read(flashDev, a, b, n) == 0;
}
bool flashErase(uint32_t a) {
  return (a == 0xE5000 || a == 0xE6000) && flash_erase(flashDev, a, 4096) == 0;
}
bool flashWrite(uint32_t a, const void *b, size_t n) {
  return a >= 0xE5000 && a + n <= 0xE7000 && flash_write(flashDev, a, b, n) == 0;
}
[[noreturn]] void rebootToBootloader() {
  nrf_power_gpregret_set(NRF_POWER, 0, 0x4e);
  sys_reboot(SYS_REBOOT_COLD);
  for (;;) {
  }
}
[[noreturn]] void fatal(const char *) {
  pin(Led, Mode::Output);
  for (;;) {
    write(Led, true);
    sleep(100);
    write(Led, false);
    sleep(900);
  }
}
} // namespace Hw
int main() {
  App::run();
}
