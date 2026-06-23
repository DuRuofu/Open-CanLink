/*
 * TWAI Loopback Test - CAN chip interface verification
 *
 * Tests the ESP32-S3 TWAI controller using internal self-test mode
 * (no external transceiver needed for basic test).
 *
 * Hardware: ESP32-S3 + TJA1051
 *   CAN_TX: GPIO4 -> TJA1051 TXD
 *   CAN_RX: GPIO5 <- TJA1051 RXD
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

static const char *TAG = "twai_test";

#define TWAI_TX_GPIO    GPIO_NUM_4
#define TWAI_RX_GPIO    GPIO_NUM_5
#define TEST_FRAME_ID   0x123

/* Supported bitrates for sweep test */
static const uint32_t BITRATES[] = {125000, 250000, 500000, 1000000};
static const char *BITRATE_NAMES[] = {"125k", "250k", "500k", "1M"};

/* Test state */
static struct {
    SemaphoreHandle_t rx_sem;
    twai_frame_t last_rx_frame;
    uint8_t last_rx_buffer[TWAI_FRAME_MAX_LEN];
    volatile bool frame_received;
    volatile int rx_count;
    volatile int tx_error_count;
    volatile int rx_error_count;
    volatile bool bus_off;
    volatile twai_error_state_t state;
} test_ctx;

/* RX callback - called from ISR context */
static IRAM_ATTR bool twai_rx_cb(twai_node_handle_t handle,
                                  const twai_rx_done_event_data_t *edata,
                                  void *user_ctx)
{
    BaseType_t woken = pdFALSE;

    test_ctx.last_rx_frame.buffer = test_ctx.last_rx_buffer;
    test_ctx.last_rx_frame.buffer_len = sizeof(test_ctx.last_rx_buffer);

    if (twai_node_receive_from_isr(handle, &test_ctx.last_rx_frame) == ESP_OK) {
        test_ctx.frame_received = true;
        test_ctx.rx_count++;
    }

    xSemaphoreGiveFromISR(test_ctx.rx_sem, &woken);
    return (woken == pdTRUE);
}

/* Error callback */
static IRAM_ATTR bool twai_error_cb(twai_node_handle_t handle,
                                     const twai_error_event_data_t *edata,
                                     void *user_ctx)
{
    ESP_EARLY_LOGW(TAG, "Bus error: 0x%lx", edata->err_flags.val);
    return false;
}

/* State change callback */
static IRAM_ATTR bool twai_state_cb(twai_node_handle_t handle,
                                     const twai_state_change_event_data_t *edata,
                                     void *user_ctx)
{
    const char *states[] = {"error_active", "error_warning", "error_passive", "bus_off"};
    ESP_EARLY_LOGI(TAG, "State: %s -> %s", states[edata->old_sta], states[edata->new_sta]);

    test_ctx.state = edata->new_sta;
    if (edata->new_sta == TWAI_ERROR_BUS_OFF) {
        test_ctx.bus_off = true;
    }
    return false;
}

/*
 * Create and start a TWAI node with given bitrate.
 * Returns node handle on success, NULL on failure.
 */
static twai_node_handle_t twai_init(uint32_t bitrate)
{
    twai_node_handle_t handle = NULL;

    twai_onchip_node_config_t config = {
        .io_cfg = {
            .tx = TWAI_TX_GPIO,
            .rx = TWAI_RX_GPIO,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = bitrate,
            .sp_permill = 800,   /* 80% sample point */
        },
        .tx_queue_depth = 5,
        .fail_retry_cnt = -1,    /* infinite retries */
        .flags = {
            .enable_self_test = true,  /* internal loopback */
        },
    };

    esp_err_t ret = twai_new_node_onchip(&config, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TWAI node: %s", esp_err_to_name(ret));
        return NULL;
    }

    twai_event_callbacks_t cbs = {
        .on_rx_done = twai_rx_cb,
        .on_error = twai_error_cb,
        .on_state_change = twai_state_cb,
    };
    ret = twai_node_register_event_callbacks(handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callbacks: %s", esp_err_to_name(ret));
        twai_node_delete(handle);
        return NULL;
    }

    ret = twai_node_enable(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TWAI node: %s", esp_err_to_name(ret));
        twai_node_delete(handle);
        return NULL;
    }

    return handle;
}

/*
 * Send a frame and wait for loopback reception.
 * Returns true if frame received matches what was sent.
 */
static bool send_and_verify(twai_node_handle_t handle, uint32_t id,
                             bool ext, const uint8_t *data, uint8_t dlc)
{
    uint8_t tx_buf[TWAI_FRAME_MAX_LEN] = {0};
    memcpy(tx_buf, data, dlc);

    twai_frame_t tx_frame = {
        .header = {
            .id = id,
            .dlc = dlc,
            { .ide = ext ? 1 : 0, .rtr = 0 },
        },
        .buffer = tx_buf,
        .buffer_len = dlc,
    };

    test_ctx.frame_received = false;

    esp_err_t ret = twai_node_transmit(handle, &tx_frame, pdMS_TO_TICKS(500));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX failed for ID 0x%lX: %s", id, esp_err_to_name(ret));
        return false;
    }

    /* Wait for RX callback to fire */
    if (xSemaphoreTake(test_ctx.rx_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "RX timeout for ID 0x%lX", id);
        return false;
    }

    if (!test_ctx.frame_received) {
        ESP_LOGE(TAG, "Frame not received for ID 0x%lX", id);
        return false;
    }

    /* Verify received frame matches sent */
    twai_frame_t *rx = &test_ctx.last_rx_frame;
    if (rx->header.id != id) {
        ESP_LOGE(TAG, "ID mismatch: sent 0x%lX, got 0x%lX", id, rx->header.id);
        return false;
    }
    if (rx->header.dlc != dlc) {
        ESP_LOGE(TAG, "DLC mismatch: sent %d, got %d", dlc, rx->header.dlc);
        return false;
    }
    if (rx->header.ide != (ext ? 1 : 0)) {
        ESP_LOGE(TAG, "EXT flag mismatch");
        return false;
    }
    if (memcmp(rx->buffer, data, dlc) != 0) {
        ESP_LOGE(TAG, "Data mismatch for ID 0x%lX", id);
        ESP_LOG_BUFFER_HEX(TAG, rx->buffer, dlc);
        return false;
    }

    return true;
}

/*
 * Test a single bitrate with standard and extended frames.
 */
static int test_bitrate(uint32_t bitrate)
{
    int passed = 0;
    int total = 0;

    ESP_LOGI(TAG, "--- Testing bitrate: %lu bps ---", bitrate);

    twai_node_handle_t handle = twai_init(bitrate);
    if (!handle) {
        ESP_LOGE(TAG, "FAIL: Could not initialize TWAI at %lu bps", bitrate);
        return -1;
    }

    /* Test 1: Standard frame (11-bit ID) with small payload */
    total++;
    uint8_t data1[] = {0xDE, 0xAD, 0xBE, 0xEF};
    if (send_and_verify(handle, 0x123, false, data1, 4)) {
        ESP_LOGI(TAG, "  [PASS] Standard frame, 4 bytes");
        passed++;
    } else {
        ESP_LOGE(TAG, "  [FAIL] Standard frame, 4 bytes");
    }

    /* Test 2: Standard frame with max payload */
    total++;
    uint8_t data2[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    if (send_and_verify(handle, 0x7FF, false, data2, 8)) {
        ESP_LOGI(TAG, "  [PASS] Standard frame (max ID), 8 bytes");
        passed++;
    } else {
        ESP_LOGE(TAG, "  [FAIL] Standard frame (max ID), 8 bytes");
    }

    /* Test 3: Extended frame (29-bit ID) */
    total++;
    uint8_t data3[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    if (send_and_verify(handle, 0x12345678, true, data3, 6)) {
        ESP_LOGI(TAG, "  [PASS] Extended frame, 6 bytes");
        passed++;
    } else {
        ESP_LOGE(TAG, "  [FAIL] Extended frame, 6 bytes");
    }

    /* Test 4: Single byte frame */
    total++;
    uint8_t data4[] = {0x42};
    if (send_and_verify(handle, 0x100, false, data4, 1)) {
        ESP_LOGI(TAG, "  [PASS] Single byte frame");
        passed++;
    } else {
        ESP_LOGE(TAG, "  [FAIL] Single byte frame");
    }

    /* Test 5: Zero data length frame */
    total++;
    if (send_and_verify(handle, 0x200, false, NULL, 0)) {
        ESP_LOGI(TAG, "  [PASS] Zero data frame (DLC=0)");
        passed++;
    } else {
        ESP_LOGE(TAG, "  [FAIL] Zero data frame (DLC=0)");
    }

    /* Clean up */
    twai_node_disable(handle);
    twai_node_delete(handle);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "  Result: %d/%d passed", passed, total);
    return (passed == total) ? 0 : 1;
}

/*
 * Burst send test - sends many frames rapidly to stress test.
 */
static void burst_test(twai_node_handle_t handle, int count)
{
    ESP_LOGI(TAG, "--- Burst test: %d frames ---", count);

    int rx_count_before = test_ctx.rx_count;

    for (int i = 0; i < count; i++) {
        uint8_t tx_buf[1] = {(uint8_t)i};
        twai_frame_t tx_frame = {
            .header = {.id = i, .dlc = 1, { .ide = 0, .rtr = 0 }},
            .buffer = tx_buf,
            .buffer_len = 1,
        };
        twai_node_transmit(handle, &tx_frame, pdMS_TO_TICKS(100));
    }

    /* Wait for all frames to be received */
    vTaskDelay(pdMS_TO_TICKS(500));

    int rx_count_after = test_ctx.rx_count;
    int received = rx_count_after - rx_count_before;
    ESP_LOGI(TAG, "  Sent: %d, Received: %d (%.1f%%)",
             count, received, (float)received / count * 100);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  TWAI Loopback Test - CAN Chip Verification");
    ESP_LOGI(TAG, "  Target: ESP32-S3, TX=GPIO4, RX=GPIO5");
    ESP_LOGI(TAG, "  Mode: Internal Self-Test (Loopback)");
    ESP_LOGI(TAG, "==============================================");

    /* Create RX semaphore */
    test_ctx.rx_sem = xSemaphoreCreateBinary();
    assert(test_ctx.rx_sem != NULL);
    test_ctx.rx_count = 0;

    int total_pass = 0;
    int total_fail = 0;

    /* Test each bitrate */
    for (size_t i = 0; i < sizeof(BITRATES) / sizeof(BITRATES[0]); i++) {
        int result = test_bitrate(BITRATES[i]);
        if (result == 0) {
            ESP_LOGI(TAG, "%s: ALL PASSED", BITRATE_NAMES[i]);
            total_pass++;
        } else if (result > 0) {
            ESP_LOGW(TAG, "%s: PARTIAL FAIL", BITRATE_NAMES[i]);
            total_fail++;
        } else {
            ESP_LOGE(TAG, "%s: INIT FAILED", BITRATE_NAMES[i]);
            total_fail++;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* Burst test at 500k (most common bitrate) */
    ESP_LOGI(TAG, "--- Running burst test at 500k ---");
    twai_node_handle_t burst_handle = twai_init(500000);
    if (burst_handle) {
        burst_test(burst_handle, 100);
        twai_node_disable(burst_handle);
        twai_node_delete(burst_handle);
    }

    /* Final report */
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  TEST COMPLETE");
    ESP_LOGI(TAG, "  Bitrates passed: %d/%d",
             total_pass, (int)(sizeof(BITRATES) / sizeof(BITRATES[0])));
    ESP_LOGI(TAG, "  Total RX frames: %d", test_ctx.rx_count);
    ESP_LOGI(TAG, "==============================================");

    if (total_fail == 0) {
        ESP_LOGI(TAG, "ALL TESTS PASSED!");
    } else {
        ESP_LOGE(TAG, "SOME TESTS FAILED - check hardware connections!");
    }

    /* Done */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Test complete. Reset to re-run.");
    }
}
