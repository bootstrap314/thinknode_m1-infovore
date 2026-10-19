#include "platform.h"
#include "usb.h"
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"
#include "semphr.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrfx_systick.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_freertos.h"
#include "ble_advdata.h"
#include "ble_srv_common.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "nrf_fstorage.h"
#include "nrf_fstorage_sd.h"
#include "fds.h"
#include "app_timer.h"
#include "app_util.h"
}
#include <algorithm>
#include <cstring>
#include <atomic>
namespace {
QueueHandle_t inputs = nullptr;
StreamBufferHandle_t gpsBytes = nullptr;
SemaphoreHandle_t flashDone = nullptr;
std::atomic<bool> overflow[2]{}, secured{false}, subscribed{false};
std::atomic<uint16_t> conn{BLE_CONN_HANDLE_INVALID};
std::atomic<bool> fdsReady{false};
std::atomic<uint32_t> flashResult{NRF_ERROR_BUSY};
uint16_t service = 0;
uint8_t uuidType = 0, advHandle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
ble_gatts_char_handles_t rxHandles{}, txHandles{};
uint8_t adBytes[31], scanBytes[31];
void (*radioIsr)() = nullptr;
void enqueue(Hw::EventKind kind, Hw::Link link, const void *data = nullptr, size_t size = 0) {
  if (!inputs)
    return;
  Hw::Event e{};
  e.kind = kind;
  e.link = link;
  e.size = std::min(size, sizeof(e.data));
  if (size)
    memcpy(e.data, data, e.size);
  if (xQueueSend(inputs, &e, 0) != pdPASS)
    overflow[unsigned(link)] = true;
}
void advertise() {
  ble_gap_adv_params_t p{};
  p.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
  p.interval = 1600;
  p.duration = 0;
  p.primary_phy = BLE_GAP_PHY_1MBPS;
  ble_gap_adv_data_t data{};
  data.adv_data = {adBytes, 21};
  data.scan_rsp_data = {scanBytes, 14};
  APP_ERROR_CHECK(sd_ble_gap_adv_set_configure(&advHandle, &data, &p));
  APP_ERROR_CHECK(sd_ble_gap_adv_start(advHandle, 1));
}
void pmEvent(const pm_evt_t *e) {
  pm_handler_on_pm_evt(e);
  pm_handler_flash_clean(e);
  if (e->evt_id == PM_EVT_CONN_SEC_SUCCEEDED) {
    secured = true;
    enqueue(Hw::EventKind::Connected, Hw::Link::Ble);
  }
  if (e->evt_id == PM_EVT_CONN_SEC_FAILED)
    sd_ble_gap_disconnect(e->conn_handle, BLE_HCI_AUTHENTICATION_FAILURE);
}
void bleEvent(const ble_evt_t *e, void *) {
  switch (e->header.evt_id) {
  case BLE_GAP_EVT_CONNECTED: {
    conn = e->evt.gap_evt.conn_handle;
    secured = false;
    subscribed = false;
    uint32_t err = pm_conn_secure(conn, false);
    // A central may already have started securing the link.
    if (err != NRF_SUCCESS && err != NRF_ERROR_BUSY)
      sd_ble_gap_disconnect(conn, BLE_HCI_AUTHENTICATION_FAILURE);
    break;
  }
  case BLE_GAP_EVT_DISCONNECTED:
    conn = BLE_CONN_HANDLE_INVALID;
    secured = false;
    subscribed = false;
    enqueue(Hw::EventKind::Disconnected, Hw::Link::Ble);
    advertise();
    break;
  case BLE_GAP_EVT_PASSKEY_DISPLAY:
    enqueue(Hw::EventKind::Passkey, Hw::Link::Ble, e->evt.gap_evt.params.passkey_display.passkey,
            6);
    break;
  case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
    sd_ble_gap_conn_param_update(conn,
                                 &e->evt.gap_evt.params.conn_param_update_request.conn_params);
    break;
  case BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST:
    // There is no nrf_ble_gatt instance to answer this on our behalf. Let
    // SoftDevice choose lengths that fit the configured connection buffers.
    APP_ERROR_CHECK(sd_ble_gap_data_length_update(e->evt.gap_evt.conn_handle, nullptr, nullptr));
    break;
  case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
    const ble_gap_phys_t phys{BLE_GAP_PHY_AUTO, BLE_GAP_PHY_AUTO};
    APP_ERROR_CHECK(sd_ble_gap_phy_update(e->evt.gap_evt.conn_handle, &phys));
    break;
  }
  case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
    sd_ble_gatts_exchange_mtu_reply(conn, 23);
    break;
  case BLE_GATTS_EVT_TIMEOUT:
  case BLE_GATTC_EVT_TIMEOUT:
    sd_ble_gap_disconnect(conn, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    break;
  case BLE_GATTS_EVT_WRITE: {
    const auto &w = e->evt.gatts_evt.params.write;
    if (w.handle == txHandles.cccd_handle && w.len == 2)
      subscribed = ble_srv_is_notification_enabled(w.data);
    else if (w.handle == rxHandles.value_handle && secured && w.offset == 0)
      enqueue(Hw::EventKind::Data, Hw::Link::Ble, w.data, w.len);
    break;
  }
  default:
    break;
  }
}
NRF_SDH_BLE_OBSERVER(appObserver, 3, bleEvent, nullptr);
void fdsEvent(const fds_evt_t *e) {
  if (e->id == FDS_EVT_INIT && e->result == NRF_SUCCESS)
    fdsReady = true;
}
void storageEvent(nrf_fstorage_evt_t *e) {
  flashResult = e->result;
  if (flashDone)
    xSemaphoreGive(flashDone);
}
NRF_FSTORAGE_DEF(nrf_fstorage_t storage) = {
    .evt_handler = storageEvent, .start_addr = 0xE5000, .end_addr = 0xE7000};
void services() {
  ble_uuid128_t base = {{0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,
                         0x00, 0x00, 0x40, 0x6e}};
  APP_ERROR_CHECK(sd_ble_uuid_vs_add(&base, &uuidType));
  ble_uuid_t id{0x0001, uuidType};
  APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &id, &service));
  ble_add_char_params_t c{};
  c.uuid_type = uuidType;
  c.uuid = 0x0002;
  c.max_len = 20;
  c.init_len = 0;
  c.is_var_len = true;
  c.char_props.write = 1;
  c.char_props.write_wo_resp = 1;
  c.write_access = SEC_MITM;
  APP_ERROR_CHECK(characteristic_add(service, &c, &rxHandles));
  c = {};
  c.uuid_type = uuidType;
  c.uuid = 0x0003;
  c.max_len = 20;
  c.init_len = 0;
  c.is_var_len = true;
  c.char_props.notify = 1;
  c.cccd_write_access = SEC_MITM;
  APP_ERROR_CHECK(characteristic_add(service, &c, &txHandles));
  adBytes[0] = 2;
  adBytes[1] = BLE_GAP_AD_TYPE_FLAGS;
  adBytes[2] = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
  adBytes[3] = 17;
  adBytes[4] = BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE;
  memcpy(adBytes + 5, base.uuid128, 16);
  adBytes[17] = 1;
  scanBytes[0] = 13;
  scanBytes[1] = BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME;
  memcpy(scanBytes + 2, "ThinkNode-M1", 12);
}
void spiConfigure(NRF_SPI_Type *dev, unsigned sck, unsigned mosi, unsigned miso) {
  dev->ENABLE = 0;
  nrf_gpio_cfg_output(sck);
  nrf_gpio_cfg_output(mosi);
  if (miso < 48)
    nrf_gpio_cfg_input(miso, NRF_GPIO_PIN_NOPULL);
  dev->PSEL.SCK = sck;
  dev->PSEL.MOSI = mosi;
  dev->PSEL.MISO = miso;
  dev->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M4;
  dev->CONFIG = 0;
}
} // namespace
extern "C" void nativeInput(unsigned kind, unsigned link, const void *p, size_t n) {
  enqueue(Hw::EventKind(kind), Hw::Link(link), p, n);
}
extern "C" void GPIOTE_IRQHandler() {
  if (NRF_GPIOTE->EVENTS_IN[0]) {
    NRF_GPIOTE->EVENTS_IN[0] = 0;
    if (radioIsr)
      radioIsr();
  }
}
extern "C" void UARTE0_UART0_IRQHandler() {
  BaseType_t wake = pdFALSE;
  if (NRF_UART0->EVENTS_RXDRDY) {
    NRF_UART0->EVENTS_RXDRDY = 0;
    uint8_t b = NRF_UART0->RXD;
    xStreamBufferSendFromISR(gpsBytes, &b, 1, &wake);
  }
  if (NRF_UART0->EVENTS_ERROR) {
    NRF_UART0->EVENTS_ERROR = 0;
    NRF_UART0->ERRORSRC = NRF_UART0->ERRORSRC;
    NRF_UART0->TASKS_STARTRX = 1;
  }
  portYIELD_FROM_ISR(wake);
}
namespace Hw {
void init() {
  nrfx_systick_init();
  inputs = xQueueCreate(128, sizeof(Event));
  gpsBytes = xStreamBufferCreate(2048, 1);
  flashDone = xSemaphoreCreateBinary();
  if (!inputs || !gpsBytes || !flashDone)
    fatal("queues");
  APP_ERROR_CHECK(app_timer_init());
  APP_ERROR_CHECK(fds_register(fdsEvent));
  // Use the same MBR-first/UICR-fallback lookup as FDS below. Bootloaders
  // using the MBR address can leave the UICR location erased.
  if (BOOTLOADER_ADDRESS != 0xF4000)
    fatal("unexpected bootloader layout");
  APP_ERROR_CHECK(pm_init());
  uint32_t wait = now();
  while (!fdsReady) {
    if (uint32_t(now() - wait) > 5000)
      fatal("bond storage");
    sleep(5);
  }
  ble_gap_sec_params_t security{};
  security.bond = 1;
  security.mitm = 1;
  security.io_caps = BLE_GAP_IO_CAPS_DISPLAY_ONLY;
  security.min_key_size = 16;
  security.max_key_size = 16;
  security.kdist_own.enc = 1;
  security.kdist_own.id = 1;
  security.kdist_peer.enc = 1;
  security.kdist_peer.id = 1;
  APP_ERROR_CHECK(pm_sec_params_set(&security));
  APP_ERROR_CHECK(pm_register(pmEvent));
  ble_gap_conn_sec_mode_t mode;
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&mode);
  APP_ERROR_CHECK(sd_ble_gap_device_name_set(&mode, (const uint8_t *)"ThinkNode-M1", 12));
  ble_gap_conn_params_t params{24, 72, 4, 500};
  APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&params));
  services();
  advertise();
  APP_ERROR_CHECK(nrf_fstorage_init(&storage, &nrf_fstorage_sd, nullptr));
  spiConfigure(NRF_SPI0, 19, 22, 23);
  spiConfigure(NRF_SPI1, 31, 29, 0xffffffff);
  usbInit();
}
uint32_t now() {
  return uint32_t(uint64_t(xTaskGetTickCount()) * 1000 / configTICK_RATE_HZ);
}
uint32_t micros() {
  return uint32_t(uint64_t(xTaskGetTickCount()) * 1000000 / configTICK_RATE_HZ);
}
void sleep(uint32_t ms) {
  vTaskDelay(std::max(TickType_t(1), pdMS_TO_TICKS(ms)));
}
void delayUs(uint32_t us) {
  nrf_delay_us(us);
}
void pin(unsigned p, Mode mode) {
  if (p >= 48)
    return;
  if (mode == Mode::Output)
    nrf_gpio_cfg_output(p);
  else
    nrf_gpio_cfg_input(p, mode == Mode::PullUp ? NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_NOPULL);
}
void write(unsigned p, bool high) {
  if (p < 48)
    nrf_gpio_pin_write(p, high);
}
bool read(unsigned p) {
  return p < 48 && nrf_gpio_pin_read(p);
}
void interrupt(unsigned p, void (*fn)(), bool rising) {
  if (p != RadioIrq)
    fatal("radio IRQ");
  radioIsr = fn;
  NRF_GPIOTE->CONFIG[0] = (GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos) |
                          (p << GPIOTE_CONFIG_PSEL_Pos) |
                          ((rising ? GPIOTE_CONFIG_POLARITY_LoToHi : GPIOTE_CONFIG_POLARITY_HiToLo)
                           << GPIOTE_CONFIG_POLARITY_Pos);
  NRF_GPIOTE->EVENTS_IN[0] = 0;
  NRF_GPIOTE->INTENSET = 1;
  sd_nvic_SetPriority(GPIOTE_IRQn, 6);
  sd_nvic_EnableIRQ(GPIOTE_IRQn);
}
void detach(unsigned) {
  NRF_GPIOTE->INTENCLR = 1;
  NRF_GPIOTE->CONFIG[0] = 0;
  radioIsr = nullptr;
}
void spi(Bus bus, const uint8_t *tx, uint8_t *rx, size_t n) {
  NRF_SPI_Type *dev = bus == Bus::Radio ? NRF_SPI0 : NRF_SPI1;
  dev->ENABLE = SPI_ENABLE_ENABLE_Enabled;
  while (n--) {
    dev->EVENTS_READY = 0;
    dev->TXD = tx ? *tx++ : 0;
    uint32_t start = now();
    while (!dev->EVENTS_READY)
      if (uint32_t(now() - start) > 20)
        fatal("SPI timeout");
    uint8_t v = dev->RXD;
    if (rx)
      *rx++ = v;
  }
  dev->ENABLE = 0;
}
void gps(bool awake) {
  pin(GpsWake, Mode::Output);
  write(GpsWake, awake);
  if (awake) {
    pin(GpsRx, Mode::Input);
    pin(GpsTx, Mode::Output);
    write(GpsTx, true);
    NRF_UART0->PSEL.RXD = GpsRx;
    NRF_UART0->PSEL.TXD = GpsTx;
    NRF_UART0->PSEL.CTS = 0xffffffff;
    NRF_UART0->PSEL.RTS = 0xffffffff;
    NRF_UART0->BAUDRATE = UART_BAUDRATE_BAUDRATE_Baud9600;
    NRF_UART0->CONFIG = 0;
    NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Enabled;
    NRF_UART0->EVENTS_RXDRDY = 0;
    NRF_UART0->EVENTS_ERROR = 0;
    NRF_UART0->INTENSET = UART_INTENSET_RXDRDY_Msk | UART_INTENSET_ERROR_Msk;
    sd_nvic_SetPriority(UARTE0_UART0_IRQn, 6);
    sd_nvic_EnableIRQ(UARTE0_UART0_IRQn);
    NRF_UART0->TASKS_STARTRX = 1;
  } else {
    NRF_UART0->INTENCLR = 0xffffffff;
    NRF_UART0->TASKS_STOPRX = 1;
    NRF_UART0->ENABLE = 0;
    sd_nvic_DisableIRQ(UARTE0_UART0_IRQn);
    pin(GpsRx, Mode::Input);
    pin(GpsTx, Mode::Input);
    xStreamBufferReset(gpsBytes);
  }
}
int gpsRead() {
  uint8_t b;
  return xStreamBufferReceive(gpsBytes, &b, 1, 0) ? b : -1;
}
float batteryVolts() {
  // EasyDMA changes this memory outside the C++ abstract machine. Without
  // volatile, an optimized build can keep the initial zero for the result.
  alignas(4) volatile int16_t sample = 0;
  NRF_SAADC->ENABLE = 1;
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
  NRF_SAADC->OVERSAMPLE = 0;
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_AnalogInput2;
  NRF_SAADC->CH[0].PSELN = 0;
  NRF_SAADC->CH[0].CONFIG = (SAADC_CH_CONFIG_GAIN_Gain1_5 << SAADC_CH_CONFIG_GAIN_Pos) |
                            (SAADC_CH_CONFIG_TACQ_40us << SAADC_CH_CONFIG_TACQ_Pos);
  NRF_SAADC->RESULT.PTR = uint32_t(&sample);
  NRF_SAADC->RESULT.MAXCNT = 1;
  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->TASKS_START = 1;
  uint32_t start = now();
  while (!NRF_SAADC->EVENTS_STARTED)
    if (uint32_t(now() - start) > 100) {
      NRF_SAADC->ENABLE = 0;
      return -1;
    }
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END)
    if (uint32_t(now() - start) > 100) {
      NRF_SAADC->ENABLE = 0;
      return -1;
    }
  NRF_SAADC->TASKS_STOP = 1;
  NRF_SAADC->ENABLE = 0;
  return sample > 0 ? sample * 3.0f * 2.02f / 4095 : -1;
}
bool event(Event &e) {
  for (unsigned i = 0; i < 2; ++i)
    if (overflow[i].exchange(false)) {
      xQueueReset(inputs);
      e = {};
      e.kind = EventKind::Overflow;
      e.link = Link(i);
      return true;
    }
  return xQueueReceive(inputs, &e, 0) == pdTRUE;
}
bool usbReady() {
  return ::usbReady();
}
bool bleReady() {
  return secured && subscribed && conn != BLE_CONN_HANDLE_INVALID;
}
bool send(Link link, const char *data, size_t size) {
  if (link == Link::Usb)
    return usbSend(data, size);
  uint32_t start = now();
  size_t offset = 0;
  while (offset < size && bleReady()) {
    uint16_t length = std::min(size - offset, size_t(20));
    ble_gatts_hvx_params_t p{};
    p.handle = txHandles.value_handle;
    p.type = BLE_GATT_HVX_NOTIFICATION;
    p.p_len = &length;
    p.p_data = (const uint8_t *)data + offset;
    uint32_t err = sd_ble_gatts_hvx(conn, &p);
    if (err == NRF_SUCCESS)
      offset += length;
    else if (err == NRF_ERROR_RESOURCES && uint32_t(now() - start) < 2000)
      sleep(10);
    else
      break;
  }
  return offset == size;
}
bool flashRead(uint32_t a, void *b, size_t n) {
  if (a < 0xE5000 || a + n > 0xE7000)
    return false;
  memcpy(b, (const void *)a, n);
  return true;
}
bool flashErase(uint32_t a) {
  if (a != 0xE5000 && a != 0xE6000)
    return false;
  xSemaphoreTake(flashDone, 0);
  flashResult = NRF_ERROR_BUSY;
  if (nrf_fstorage_erase(&storage, a, 1, nullptr) != NRF_SUCCESS)
    return false;
  if (!xSemaphoreTake(flashDone, pdMS_TO_TICKS(5000)))
    fatal("flash timeout"); // Keep asynchronous write buffers alive until reset.
  return flashResult == NRF_SUCCESS;
}
bool flashWrite(uint32_t a, const void *b, size_t n) {
  if (a < 0xE5000 || a + n > 0xE7000)
    return false;
  xSemaphoreTake(flashDone, 0);
  flashResult = NRF_ERROR_BUSY;
  if (nrf_fstorage_write(&storage, a, b, n, nullptr) != NRF_SUCCESS)
    return false;
  if (!xSemaphoreTake(flashDone, pdMS_TO_TICKS(5000)))
    fatal("flash timeout"); // Keep asynchronous write buffers alive until reset.
  return flashResult == NRF_SUCCESS;
}
[[noreturn]] void rebootToBootloader() {
  sd_power_gpregret_clr(0, 0xff);
  sd_power_gpregret_set(0, 0x4e);
  sd_nvic_SystemReset();
  for (;;) {
  }
}
[[noreturn]] void fatal(const char *) {
  nrf_gpio_cfg_output(Led);
  for (;;) {
    nrf_gpio_pin_toggle(Led);
    nrf_delay_ms(300);
  }
}
} // namespace Hw
extern "C" void vApplicationMallocFailedHook() {
  Hw::fatal("heap");
}
extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char *) {
  Hw::fatal("stack");
}
extern "C" int main() {
  APP_ERROR_CHECK(nrf_drv_clock_init());
  // app_usbd_init() reuses this driver later. Its first initialization must
  // happen before SoftDevice enable, or the SDK returns INVALID_STATE.
  APP_ERROR_CHECK(nrf_drv_power_init(nullptr));
  APP_ERROR_CHECK(nrf_sdh_enable_request());
  uint32_t ram = 0x20006000;
  APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(1, &ram));
  APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram));
  nrf_sdh_freertos_init(nullptr, nullptr);
  if (xTaskCreate([](void *) { App::run(); }, "M1", 4096, nullptr, 1, nullptr) != pdPASS)
    Hw::fatal("application task");
  vTaskStartScheduler();
  Hw::fatal("scheduler");
}

extern "C" void nativeFatal(const char *reason) {
  Hw::fatal(reason);
}

extern "C" void nativeAssert(void) {
  Hw::fatal("assert");
}
