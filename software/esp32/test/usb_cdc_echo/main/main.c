/*
 * USB CDC Echo Test - USB serial port verification
 *
 * Tests the ESP32-S3 USB-OTG CDC ACM (virtual serial port).
 * Echoes all received data back to the host.
 *
 * Hardware: ESP32-S3
 *   USB_DP: GPIO20 (fixed OTG pin)
 *   USB_DM: GPIO19 (fixed OTG pin)
 *
 * Verify: Connect to PC, open serial terminal, type characters,
 *         confirm they are echoed back.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "sdkconfig.h"

static const char *TAG = "usb_echo";

/* Queue message for passing received data from callback to task */
typedef struct {
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t len;
    uint8_t itf;
} rx_msg_t;

static QueueHandle_t rx_queue;
static volatile uint32_t total_rx_bytes = 0;
static volatile uint32_t total_tx_bytes = 0;

/*
 * CDC RX callback - called when data arrives from USB host.
 * Reads data and pushes to FreeRTOS queue for the echo task.
 */
static void usb_rx_callback(int itf, cdcacm_event_t *event)
{
    rx_msg_t msg = { .itf = itf };
    esp_err_t ret = tinyusb_cdcacm_read(itf, msg.buf,
                                         sizeof(msg.buf) - 1, &msg.len);
    if (ret == ESP_OK && msg.len > 0) {
        total_rx_bytes += msg.len;
        /* Non-blocking send - if queue is full, drop oldest */
        if (xQueueSend(rx_queue, &msg, 0) != pdTRUE) {
            /* Queue full - drain one and retry */
            rx_msg_t old;
            xQueueReceive(rx_queue, &old, 0);
            xQueueSend(rx_queue, &msg, 0);
        }
    }
}

/*
 * USB line state callback - notifies when DTR/RTS change (host connects/disconnects).
 */
static void usb_line_state_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "CDC%d line state: DTR=%d, RTS=%d", itf, dtr, rts);
}

/*
 * Echo task - drains the RX queue and echoes data back to USB CDC.
 */
static void echo_task(void *arg)
{
    rx_msg_t msg;
    ESP_LOGI(TAG, "Echo task started");

    while (1) {
        if (xQueueReceive(rx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            /* Echo data back to the same CDC interface */
            tinyusb_cdcacm_write_queue(msg.itf, msg.buf, msg.len);
            esp_err_t ret = tinyusb_cdcacm_write_flush(msg.itf, 0);
            if (ret == ESP_OK) {
                total_tx_bytes += msg.len;
            } else {
                ESP_LOGW(TAG, "Write flush error: %s", esp_err_to_name(ret));
            }
        }
    }
}

/*
 * Status task - periodically prints byte counts to UART0 console.
 */
static void status_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Stats: RX=%lu TX=%lu bytes",
                 (unsigned long)total_rx_bytes,
                 (unsigned long)total_tx_bytes);
        ESP_LOGI(TAG, "Queue: %d/%d msgs waiting",
                 uxQueueMessagesWaiting(rx_queue),
                 CONFIG_TINYUSB_CDC_RX_BUFSIZE > 0 ? 5 : 0);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  USB CDC Echo Test");
    ESP_LOGI(TAG, "  Target: ESP32-S3, USB-OTG (DP=GPIO20, DM=GPIO19)");
    ESP_LOGI(TAG, "==============================================");

    /* Create RX queue (holds 5 messages) */
    rx_queue = xQueueCreate(5, sizeof(rx_msg_t));
    assert(rx_queue != NULL);

    /* Install TinyUSB driver */
    ESP_LOGI(TAG, "Initializing TinyUSB...");
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "TinyUSB driver installed");

    /* Configure CDC ACM */
    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &usb_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));

    /* Register line state callback separately */
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
                        TINYUSB_CDC_ACM_0,
                        CDC_EVENT_LINE_STATE_CHANGED,
                        &usb_line_state_callback));

    ESP_LOGI(TAG, "CDC ACM initialized on port 0");
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  Connect USB cable to PC");
    ESP_LOGI(TAG, "  Open serial terminal (baudrate ignored for CDC)");
    ESP_LOGI(TAG, "  Type characters - they will be echoed back");
    ESP_LOGI(TAG, "==============================================");

    /* Create tasks */
    xTaskCreate(echo_task, "echo", 4096, NULL, 5, NULL);
    xTaskCreate(status_task, "status", 2048, NULL, 2, NULL);

    /* Idle loop */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
