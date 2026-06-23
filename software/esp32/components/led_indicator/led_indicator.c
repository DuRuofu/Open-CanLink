/*
 * led_indicator.c - Minimal WS2812 status LED for esp-can-link
 *
 * Uses ESP32 RMT peripheral directly, no external component dependencies.
 * Drives a single WS2812 on GPIO21:
 *   Off — not initialized
 *   Blue dim — CAN stopped (ready)
 *   Green dim — CAN running
 *   Red dim — CAN bus-off / error
 *
 * WS2812 timing (RGB order, 800kHz):
 *   0 code: 0.4us H + 0.85us L
 *   1 code: 0.8us H + 0.45us L
 *   RESET:  >50us low
 * At 10MHz RMT resolution (0.1us per tick)
 */

#include "led_indicator.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "led";

#define T0H  4   /* 0.4us */
#define T0L  8   /* 0.8us */
#define T1H  8   /* 0.8us */
#define T1L  4   /* 0.4us */
#define RST  600 /* >50us = 500 ticks, use 600 for margin */

static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_encoder = NULL;
static bool s_initialized = false;
static led_state_t s_last_state = LED_STATE_OFF;

/*
 * Build RMT symbols for one WS2812 pixel (RGB, 24 bits) + reset.
 * Returns number of symbols written (always 25).
 */
static int make_ws2812_symbols(const uint8_t rgb[3], rmt_symbol_word_t *symbols)
{
    int idx = 0;
    for (int byte = 0; byte < 3; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            if (rgb[byte] & (1 << bit)) {
                symbols[idx].duration0 = T1H;
                symbols[idx].level0 = 1;
                symbols[idx].duration1 = T1L;
                symbols[idx].level1 = 0;
            } else {
                symbols[idx].duration0 = T0H;
                symbols[idx].level0 = 1;
                symbols[idx].duration1 = T0L;
                symbols[idx].level1 = 0;
            }
            idx++;
        }
    }
    symbols[idx].duration0 = 0;
    symbols[idx].level0 = 0;
    symbols[idx].duration1 = RST;
    symbols[idx].level1 = 0;
    idx++;
    return idx;
}

esp_err_t led_indicator_init(uint8_t gpio_num)
{
    if (s_initialized) return ESP_OK;

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_chan), TAG, "rmt channel");

    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &s_encoder), TAG, "encoder");

    ESP_RETURN_ON_ERROR(rmt_enable(s_chan), TAG, "rmt enable");
    s_initialized = true;

    led_indicator_set_state(LED_STATE_OFF);
    ESP_LOGI(TAG, "Initialized on GPIO%d", gpio_num);
    return ESP_OK;
}

esp_err_t led_indicator_set_state(led_state_t state)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_last_state = state;

    uint8_t r = 0, g = 0, b = 0;
    switch (state) {
    case LED_STATE_OFF:     r = 0;  g = 0;  b = 0;   break;
    case LED_STATE_READY:   r = 0;  g = 0;  b = 20;  break;
    case LED_STATE_RUNNING: r = 0;  g = 20; b = 0;   break;
    case LED_STATE_ERROR:   r = 20; g = 0;  b = 0;   break;
    }

    uint8_t rgb[3] = {r, g, b};
    rmt_symbol_word_t symbols[25];
    int sym_count = make_ws2812_symbols(rgb, symbols);

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    esp_err_t ret = rmt_transmit(s_chan, s_encoder, symbols,
                                  sym_count * sizeof(rmt_symbol_word_t), &tx_conf);
    if (ret == ESP_OK) {
        rmt_tx_wait_all_done(s_chan, 5);
    }
    return ret;
}

void led_indicator_blip(void)
{
    if (!s_initialized) return;

    /* Toggle: alternate between off and current state color on each call */
    static bool s_toggle;
    s_toggle = !s_toggle;

    if (s_toggle) {
        /* Off */
        uint8_t off[3] = {0, 0, 0};
        rmt_symbol_word_t sym[25];
        int n = make_ws2812_symbols(off, sym);
        rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
        rmt_transmit(s_chan, s_encoder, sym, n * sizeof(rmt_symbol_word_t), &tx_cfg);
    } else {
        /* Restore last state */
        led_indicator_set_state(s_last_state);
    }
}

esp_err_t led_indicator_deinit(void)
{
    if (!s_initialized) return ESP_OK;

    led_indicator_set_state(LED_STATE_OFF);
    rmt_tx_wait_all_done(s_chan, -1);
    rmt_disable(s_chan);
    rmt_del_encoder(s_encoder);
    rmt_del_channel(s_chan);
    s_chan = NULL;
    s_encoder = NULL;
    s_initialized = false;
    return ESP_OK;
}
