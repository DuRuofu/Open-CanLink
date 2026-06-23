/*
 * can_driver.c - TWAI driver wrapper implementation
 *
 * Adapted from software/ref/twai/twai_network/ and twai_utils/ reference code.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "can_driver.h"

static const char *TAG = "can_drv";

/* ---- Internal state ---- */
static twai_node_handle_t s_node_handle = NULL;
static bool s_is_running = false;
static SemaphoreHandle_t s_tx_sem = NULL;
static volatile bool s_needs_recovery = false;

/* RX callback */
static can_rx_callback_t s_rx_cb = NULL;
static void *s_rx_user_ctx = NULL;

/* RX queue: bridges ISR callback to task */
#define RX_QUEUE_DEPTH 64
typedef struct {
    uint32_t id;
    bool ext;
    uint8_t dlc;
    uint8_t data[TWAI_FRAME_MAX_LEN];
    uint32_t timestamp_us;
} rx_item_t;

static QueueHandle_t s_rx_queue = NULL;

/* ---- TWAI event callbacks (called from ISR context) ---- */

static IRAM_ATTR bool twai_rx_done_cb(twai_node_handle_t handle,
                                       const twai_rx_done_event_data_t *edata,
                                       void *user_ctx)
{
    rx_item_t item = {0};
    twai_frame_t rx_frame = {
        .buffer = item.data,
        .buffer_len = sizeof(item.data),
    };

    esp_err_t ret = twai_node_receive_from_isr(handle, &rx_frame);
    if (ret == ESP_OK) {
        item.id = rx_frame.header.id;
        item.ext = rx_frame.header.ide;
        item.dlc = rx_frame.header.dlc;
        item.timestamp_us = (uint32_t)rx_frame.header.timestamp;

        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(s_rx_queue, &item, &woken);
        return (woken == pdTRUE);
    }
    return false;
}

static IRAM_ATTR bool twai_error_cb(twai_node_handle_t handle,
                                     const twai_error_event_data_t *edata,
                                     void *user_ctx)
{
    ESP_EARLY_LOGW(TAG, "Bus error: 0x%lx", edata->err_flags.val);
    return false;
}

static IRAM_ATTR bool twai_state_cb(twai_node_handle_t handle,
                                     const twai_state_change_event_data_t *edata,
                                     void *user_ctx)
{
    const char *states[] = {"error_active", "error_warning", "error_passive", "bus_off"};
    ESP_EARLY_LOGI(TAG, "State: %s -> %s", states[edata->old_sta], states[edata->new_sta]);

    if (edata->new_sta == TWAI_ERROR_BUS_OFF) {
        s_needs_recovery = true;
    }
    return false;
}

/* ---- Public API ---- */

esp_err_t can_driver_init(gpio_num_t tx_pin, gpio_num_t rx_pin, uint32_t bitrate)
{
    if (s_node_handle != NULL) {
        ESP_LOGW(TAG, "Driver already initialized, stopping first");
        can_driver_stop();
    }

    /* Create sync primitives */
    if (s_tx_sem == NULL) {
        s_tx_sem = xSemaphoreCreateMutex();
    }
    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_item_t));
    }

    twai_onchip_node_config_t config = {
        .io_cfg = {
            .tx = tx_pin,
            .rx = rx_pin,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = bitrate,
            .sp_permill = 800,   /* 80% sample point */
        },
        .tx_queue_depth = 10,
        .fail_retry_cnt = -1,    /* infinite retries */
        .flags = {
            .enable_self_test = false,
            .enable_loopback = false,
            .enable_listen_only = false,
            .no_receive_rtr = false,
        },
    };

    ESP_RETURN_ON_ERROR(
        twai_new_node_onchip(&config, &s_node_handle),
        TAG, "Failed to create TWAI node");

    twai_event_callbacks_t cbs = {
        .on_rx_done = twai_rx_done_cb,
        .on_error = twai_error_cb,
        .on_state_change = twai_state_cb,
    };
    ESP_RETURN_ON_ERROR(
        twai_node_register_event_callbacks(s_node_handle, &cbs, NULL),
        TAG, "Failed to register callbacks");

    /* Don't enable yet - wait for can_driver_start() */
    s_is_running = false;
    s_needs_recovery = false;

    ESP_LOGI(TAG, "Initialized: TX=GPIO%d, RX=GPIO%d, bitrate=%lu",
             tx_pin, rx_pin, bitrate);
    return ESP_OK;
}

esp_err_t can_driver_start(void)
{
    if (s_node_handle == NULL) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_is_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        twai_node_enable(s_node_handle),
        TAG, "Failed to enable TWAI node");
    s_is_running = true;
    ESP_LOGI(TAG, "CAN driver started");
    return ESP_OK;
}

esp_err_t can_driver_stop(void)
{
    if (s_node_handle == NULL) {
        return ESP_OK;
    }
    if (s_is_running) {
        twai_node_disable(s_node_handle);
        s_is_running = false;
        ESP_LOGI(TAG, "CAN driver stopped");
    }
    return ESP_OK;
}

esp_err_t can_driver_set_bitrate(uint32_t bitrate)
{
    bool was_running = s_is_running;

    /* Stop and delete current node */
    can_driver_stop();
    if (s_node_handle) {
        twai_node_delete(s_node_handle);
        s_node_handle = NULL;
    }

    /* Read current GPIO config from old? We stored it... but for simplicity,
     * reconstruct with default pins. Better approach: store init config. */
    /* For now, re-init with default GPIO4/GPIO5 */
    esp_err_t ret = can_driver_init(GPIO_NUM_4, GPIO_NUM_5, bitrate);
    if (ret != ESP_OK) {
        return ret;
    }

    if (was_running) {
        ret = can_driver_start();
    }
    return ret;
}

esp_err_t can_driver_transmit(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data)
{
    if (!s_is_running || s_node_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx_buf[TWAI_FRAME_MAX_LEN] = {0};
    if (data && dlc > 0) {
        memcpy(tx_buf, data, dlc);
    }

    twai_frame_t tx_frame = {
        .header = {
            .id = id,
            .dlc = dlc,
            { .ide = ext ? 1 : 0, .rtr = 0 },
        },
        .buffer = tx_buf,
        .buffer_len = dlc,
    };

    /* Serialize transmits */
    if (xSemaphoreTake(s_tx_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = twai_node_transmit(s_node_handle, &tx_frame, pdMS_TO_TICKS(500));
    xSemaphoreGive(s_tx_sem);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX failed for ID 0x%lX: %s", id, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t can_driver_get_status(can_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;

    if (!s_is_running || s_node_handle == NULL) {
        status->state = CAN_STATE_STOPPED;
        status->tx_errors = 0;
        status->rx_errors = 0;
        status->bus_off = false;
        status->tx_queue_remaining = 0;
        return ESP_OK;
    }

    twai_node_status_t node_status;
    esp_err_t ret = twai_node_get_info(s_node_handle, &node_status, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    switch (node_status.state) {
    case TWAI_ERROR_ACTIVE:
    case TWAI_ERROR_WARNING:
    case TWAI_ERROR_PASSIVE:
        status->state = CAN_STATE_RUNNING;
        break;
    case TWAI_ERROR_BUS_OFF:
        status->state = CAN_STATE_BUS_OFF;
        break;
    default:
        status->state = CAN_STATE_STOPPED;
        break;
    }

    status->tx_errors = node_status.tx_error_count;
    status->rx_errors = node_status.rx_error_count;
    status->bus_off = (node_status.state == TWAI_ERROR_BUS_OFF);
    status->tx_queue_remaining = node_status.tx_queue_remaining;

    return ESP_OK;
}

void can_driver_set_rx_callback(can_rx_callback_t cb, void *user_ctx)
{
    s_rx_cb = cb;
    s_rx_user_ctx = user_ctx;
}

esp_err_t can_driver_set_filter(uint32_t id, uint32_t mask, bool ext)
{
    if (s_node_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    twai_mask_filter_config_t filter = {
        .id = id,
        .mask = mask,
        { .is_ext = ext ? 1 : 0 },
    };
    return twai_node_config_mask_filter(s_node_handle, 0, &filter);
}

esp_err_t can_driver_recover(void)
{
    if (s_node_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting bus-off recovery...");
    esp_err_t ret = twai_node_recover(s_node_handle);
    if (ret == ESP_OK) {
        s_needs_recovery = false;
    }
    return ret;
}

/*
 * RX processing task - drains ISR queue and invokes user callback.
 */
static void can_rx_task(void *arg)
{
    rx_item_t item;
    ESP_LOGI(TAG, "RX task started");

    while (1) {
        if (xQueueReceive(s_rx_queue, &item, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (s_rx_cb) {
                s_rx_cb(item.id, item.ext, item.dlc,
                        item.data, item.timestamp_us, s_rx_user_ctx);
            }
        }

        /* Check for bus-off recovery */
        if (s_needs_recovery && s_is_running) {
            ESP_LOGW(TAG, "Bus-off detected, attempting recovery...");
            can_driver_recover();
        }
    }
}

/* Auto-start the RX task from app_main */
__attribute__((constructor))
static void _can_driver_auto_init(void)
{
    /* RX task will be started by can_bridge app_main.
     * This is safe because constructors run before app_main. */
}

/* Public function to start RX task (call from app_main) */
void can_driver_start_rx_task(void)
{
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 8, NULL);
}
