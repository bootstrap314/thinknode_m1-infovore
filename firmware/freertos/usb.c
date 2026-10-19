#include "usb.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "app_usbd.h"
#include "app_usbd_cdc_acm.h"
#include <stdio.h>
#include "nrf_drv_usbd.h"

#include <string.h>
#include <stdbool.h>
extern void nativeFatal(const char *);

uint8_t g_extern_serial_number[17];
TaskHandle_t usbTask = NULL;
QueueHandle_t txQueue = NULL;
typedef struct {
  size_t size;
  char data[64];
} Chunk;
volatile bool open = false, txBusy = false;
char rxByte;
Chunk active = {0};
static void cdcEvent(const app_usbd_class_inst_t *, app_usbd_cdc_acm_user_event_t);
APP_USBD_CDC_ACM_GLOBAL_DEF(cdc, cdcEvent, 0, 1, NRF_DRV_USBD_EPIN2, NRF_DRV_USBD_EPIN1,
                            NRF_DRV_USBD_EPOUT1, APP_USBD_CDC_COMM_PROTOCOL_AT_V250);
static void cdcEvent(const app_usbd_class_inst_t *inst, app_usbd_cdc_acm_user_event_t e) {
  switch (e) {
  case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
    open = true;
    app_usbd_cdc_acm_read(&cdc, &rxByte, 1);
    break;
  case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
    open = false;
    txBusy = false;
    xQueueReset(txQueue);
    break;
  case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
    txBusy = false;
    break;
  case APP_USBD_CDC_ACM_USER_EVT_RX_DONE: {
    char bytes[64];
    size_t n = 0;
    ret_code_t result;
    do {
      bytes[n++] = rxByte;
      if (n == sizeof(bytes)) {
        nativeInput(0, 0, bytes, n);
        n = 0;
      }
      result = app_usbd_cdc_acm_read(&cdc, &rxByte, 1);
    } while (result == NRF_SUCCESS);
    if (n)
      nativeInput(0, 0, bytes, n);
    break;
  }
  default:
    break;
  }
}
void notify() {
  if (!usbTask)
    return;
  if (__get_IPSR()) {
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(usbTask, &wake);
    portYIELD_FROM_ISR(wake);
  } else
    xTaskNotifyGive(usbTask);
}
void irqEvent(const app_usbd_internal_evt_t *event, bool queued) {
  notify();
}
void stateEvent(app_usbd_event_type_t e) {
  switch (e) {
  case APP_USBD_EVT_STOPPED:
    app_usbd_disable();
    xQueueReset(txQueue);
    open = false;
    txBusy = false;
    break;
  case APP_USBD_EVT_POWER_DETECTED:
    if (!nrf_drv_usbd_is_enabled())
      app_usbd_enable();
    break;
  case APP_USBD_EVT_POWER_REMOVED:
    app_usbd_stop();
    break;
  case APP_USBD_EVT_POWER_READY:
    app_usbd_start();
    break;
  default:
    break;
  }
}
void task(void *unused) {
  snprintf((char *)g_extern_serial_number, sizeof(g_extern_serial_number), "%08lX%08lX",
           (unsigned long)NRF_FICR->DEVICEID[1], (unsigned long)NRF_FICR->DEVICEID[0]);
  app_usbd_config_t cfg = {0};
  cfg.ev_isr_handler = irqEvent;
  cfg.ev_state_proc = stateEvent;
  APP_ERROR_CHECK(app_usbd_init(&cfg));
  APP_ERROR_CHECK(app_usbd_class_append(app_usbd_cdc_acm_class_inst_get(&cdc)));
  APP_ERROR_CHECK(app_usbd_power_events_enable());
  for (;;) {
    while (app_usbd_event_queue_process()) {
    }
    if (open && !txBusy && xQueueReceive(txQueue, &active, 0) == pdTRUE) {
      txBusy = true;
      if (app_usbd_cdc_acm_write(&cdc, active.data, active.size) != NRF_SUCCESS)
        txBusy = false;
    }
    // USB interrupts or a new outbound chunk wake this task. No idle polling.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}
void usbInit() {
  txQueue = xQueueCreate(16, sizeof(Chunk));
  if (!txQueue || xTaskCreate(task, "USB", 768, NULL, 2, &usbTask) != pdPASS)
    nativeFatal("USB task");
}
bool usbReady(void) {
  return open;
}
bool usbSend(const char *data, size_t size) {
  if (!open)
    return false;
  while (size) {
    Chunk chunk = {0};
    chunk.size = (size < sizeof(chunk.data) ? size : sizeof(chunk.data));
    memcpy(chunk.data, data, chunk.size);
    if (xQueueSend(txQueue, &chunk, pdMS_TO_TICKS(250)) != pdTRUE)
      return false;
    notify();
    data += chunk.size;
    size -= chunk.size;
  }
  return true;
}
