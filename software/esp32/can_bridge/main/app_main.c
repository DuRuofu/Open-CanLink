/*
 * app_main.c - esp-can-link bridge firmware entry point
 *
 * USB CDC <=> CAN Bridge with JSON protocol.
 * Wire-up: USB CDC RX -> protocol parser -> command handler -> CAN driver
 *          CAN driver RX -> protocol formatter -> USB CDC TX
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "can_driver.h"
#include "usb_cdc.h"
#include "protocol.h"
#include "command_handler.h"

static const char *TAG = "bridge";

/* Default CAN pins */
#define CAN_TX_GPIO      GPIO_NUM_4
#define CAN_RX_GPIO      GPIO_NUM_5
#define CAN_STANDBY_GPIO GPIO_NUM_6   /* TJA1051 S pin, LOW=normal, HIGH=standby */

/* ---- Forward declarations ---- */
static void usb_rx_callback(const uint8_t *data, size_t len, void *user_ctx);
static void can_rx_callback(uint32_t id, bool ext, uint8_t dlc,
                              const uint8_t *data, uint32_t timestamp_us,
                              void *user_ctx);
static void bridge_task(void *arg);
static void periodic_task_fn(void *arg);

/* ---- Data structures ---- */

/* USB RX queue: raw bytes from USB, processed by bridge task */
#define USB_RX_QUEUE_DEPTH 32
typedef struct {
    uint8_t data[256];
    size_t len;
} usb_rx_item_t;

static QueueHandle_t s_usb_rx_queue = NULL;

/* ---- USB RX callback (called from usb_cdc component's RX task) ---- */

static void usb_rx_callback(const uint8_t *data, size_t len, void *user_ctx)
{
    if (!data || len == 0) return;

    usb_rx_item_t item = { .len = len };
    size_t copy = len > sizeof(item.data) ? sizeof(item.data) : len;
    memcpy(item.data, data, copy);

    /* Non-blocking push */
    if (xQueueSend(s_usb_rx_queue, &item, 0) != pdTRUE) {
        /* Drain oldest */
        usb_rx_item_t old;
        xQueueReceive(s_usb_rx_queue, &old, 0);
        xQueueSend(s_usb_rx_queue, &item, 0);
    }
}

/* ---- CAN RX callback (called from can_driver component's RX task) ---- */

static void can_rx_callback(uint32_t id, bool ext, uint8_t dlc,
                              const uint8_t *data, uint32_t timestamp_us,
                              void *user_ctx)
{
    char json_line[256];
    uint32_t timestamp_ms = timestamp_us / 1000;

    int len = protocol_build_rx_frame(id, ext, dlc, data, timestamp_ms,
                                       json_line, sizeof(json_line));
    if (len > 0) {
        usb_cdc_write((const uint8_t *)json_line, (size_t)len);
    }
}

/* ---- Main bridge task: process USB -> protocol -> CAN ---- */

static void bridge_task(void *arg)
{
    usb_rx_item_t item;
    parsed_cmd_t cmd;
    char response[256];

    ESP_LOGI(TAG, "Bridge task started");

    while (1) {
        if (xQueueReceive(s_usb_rx_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            /* Feed each byte to the protocol line buffer */
            for (size_t i = 0; i < item.len; i++) {
                protocol_feed_byte((char)item.data[i]);
            }

            /* Drain any complete lines */
            char line[512];
            while (protocol_get_line(line, sizeof(line)) > 0) {
                ESP_LOGD(TAG, "CMD: %s", line);

                if (protocol_parse(line, &cmd)) {
                    int rlen = command_handler_process(&cmd, response, sizeof(response));
                    if (rlen > 0) {
                        usb_cdc_write((const uint8_t *)response, (size_t)rlen);
                    }
                } else {
                    /* Invalid JSON - send error */
                    int rlen = protocol_build_response(NULL, false,
                                                        "Invalid JSON", response, sizeof(response));
                    usb_cdc_write((const uint8_t *)response, (size_t)rlen);
                }
            }
        }
    }
}

/* ---- Periodic CAN transmit task ---- */

static void periodic_task_fn(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    ESP_LOGI(TAG, "Periodic TX task started");

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
        command_handler_periodic_tick();
    }
}

/* ---- Status reporting task ---- */

static void status_task_fn(void *arg)
{
    ESP_LOGI(TAG, "Status task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));  /* every 30 seconds */

        can_status_t status;
        if (can_driver_get_status(&status) == ESP_OK) {
            const char *state_name;
            switch (status.state) {
            case CAN_STATE_RUNNING: state_name = "running"; break;
            case CAN_STATE_BUS_OFF: state_name = "bus_off"; break;
            default:                state_name = "stopped"; break;
            }
            ESP_LOGI(TAG, "CAN: %s | TX_ERR=%u RX_ERR=%u | TX_QUEUE=%lu",
                     state_name, status.tx_errors, status.rx_errors,
                     (unsigned long)status.tx_queue_remaining);
        }
    }
}

/* ---- Main ---- */

void app_main(void)
{
    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "  esp-can-link Bridge Firmware v0.1.0");
    ESP_LOGI(TAG, "  HW: ESP32-S3 + TJA1051");
    ESP_LOGI(TAG, "  CAN: TX=GPIO%d, RX=GPIO%d, S=GPIO%d", CAN_TX_GPIO, CAN_RX_GPIO, CAN_STANDBY_GPIO);
    ESP_LOGI(TAG, "=============================================");

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize CAN driver (stopped by default per spec) */
    ESP_ERROR_CHECK(can_driver_init(CAN_TX_GPIO, CAN_RX_GPIO, 500000, CAN_STANDBY_GPIO));
    can_driver_set_rx_callback(can_rx_callback, NULL);

    /* Initialize USB CDC */
    ESP_ERROR_CHECK(usb_cdc_init());
    usb_cdc_set_rx_callback(usb_rx_callback, NULL);

    /* Initialize command handler */
    command_handler_init();

    /* Create USB RX queue */
    s_usb_rx_queue = xQueueCreate(USB_RX_QUEUE_DEPTH, sizeof(usb_rx_item_t));
    assert(s_usb_rx_queue != NULL);

    /* Create tasks */
    xTaskCreate(bridge_task, "bridge", 4096, NULL, 5, NULL);
    xTaskCreate(periodic_task_fn, "periodic", 2048, NULL, 3, NULL);
    xTaskCreate(status_task_fn, "status_rpt", 2048, NULL, 1, NULL);

    /* Start component-internal RX tasks */
    usb_cdc_start_rx_task();
    can_driver_start_rx_task();

    ESP_LOGI(TAG, "Bridge ready. Connect USB and send JSON commands.");
    ESP_LOGI(TAG, "Default: CAN stopped. Send {\"cmd\":\"can_start\"} to begin.");

    /* Idle */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
