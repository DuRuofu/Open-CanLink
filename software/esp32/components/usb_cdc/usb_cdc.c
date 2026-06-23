/*
 * usb_cdc.c - USB CDC ACM wrapper implementation
 *
 * Adapted from software/ref/tusb_serial_device/ reference code.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "sdkconfig.h"
#include "usb_cdc.h"

static const char *TAG = "usb_cdc";

/* Internal state */
static SemaphoreHandle_t s_write_mutex = NULL;

/* RX queue and callback */
#define RX_QUEUE_DEPTH 16
typedef struct {
    uint8_t data[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t len;
} cdc_rx_item_t;

static QueueHandle_t s_rx_queue = NULL;
static usb_cdc_rx_callback_t s_rx_cb = NULL;
static void *s_rx_user_ctx = NULL;

/* USB RX callback from TinyUSB - called from USB task context */
static void tinyusb_rx_callback(int itf, cdcacm_event_t *event)
{
    cdc_rx_item_t item = {0};
    esp_err_t ret = tinyusb_cdcacm_read(itf, item.data,
                                         sizeof(item.data), &item.len);
    if (ret == ESP_OK && item.len > 0) {
        /* Non-blocking send to queue; if full, oldest is dropped */
        if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
            cdc_rx_item_t old;
            xQueueReceive(s_rx_queue, &old, 0);
            xQueueSend(s_rx_queue, &item, 0);
        }
    }
}

/* Line state change callback */
static void tinyusb_line_state_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state: DTR=%d, RTS=%d", dtr, rts);
}

esp_err_t usb_cdc_init(void)
{
    /* Create sync primitives */
    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(cdc_rx_item_t));
        if (!s_rx_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_write_mutex == NULL) {
        s_write_mutex = xSemaphoreCreateMutex();
        if (!s_write_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* Install TinyUSB driver */
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(
        tinyusb_driver_install(&tusb_cfg),
        TAG, "Failed to install TinyUSB driver");

    /* Configure CDC ACM */
    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &tinyusb_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_init(&acm_cfg),
        TAG, "Failed to init CDC ACM");

    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_register_callback(
            TINYUSB_CDC_ACM_0,
            CDC_EVENT_LINE_STATE_CHANGED,
            &tinyusb_line_state_callback),
        TAG, "Failed to register line state callback");

    ESP_LOGI(TAG, "USB CDC initialized on port 0");
    return ESP_OK;
}

void usb_cdc_set_rx_callback(usb_cdc_rx_callback_t cb, void *user_ctx)
{
    s_rx_cb = cb;
    s_rx_user_ctx = user_ctx;
}

esp_err_t usb_cdc_write(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_OK;
    }

    if (xSemaphoreTake(s_write_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data, len);
    esp_err_t ret = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);

    xSemaphoreGive(s_write_mutex);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Write flush error: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t usb_cdc_write_str(const char *str)
{
    if (!str) return ESP_OK;
    return usb_cdc_write((const uint8_t *)str, strlen(str));
}

/*
 * RX processing task - drains the RX queue and invokes user callback.
 */
static void usb_cdc_rx_task(void *arg)
{
    cdc_rx_item_t item;
    ESP_LOGI(TAG, "RX task started");

    while (1) {
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) == pdTRUE) {
            if (s_rx_cb) {
                s_rx_cb(item.data, item.len, s_rx_user_ctx);
            }
        }
    }
}

/* Public function to start RX task (call from app_main) */
void usb_cdc_start_rx_task(void)
{
    xTaskCreate(usb_cdc_rx_task, "usb_cdc_rx", 4096, NULL, 5, NULL);
}
