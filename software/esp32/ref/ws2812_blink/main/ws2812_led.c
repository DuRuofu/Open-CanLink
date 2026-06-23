#include "ws2812_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ws2812_led";

static led_strip_handle_t led_strip = NULL;
static uint16_t led_count = 0;
static bool is_initialized = false;

/**
 * @brief WS2812 LED组件初始化
 */
esp_err_t ws2812_led_init(uint8_t gpio_num, uint16_t led_num)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "WS2812 LED已经初始化");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "初始化 WS2812 LED，GPIO: %d，LED数量: %d", gpio_num, led_num);
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = led_num,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建LED strip设备失败: %s", esp_err_to_name(ret));
        return ret;
    }

    led_count = led_num;
    is_initialized = true;
    
    // 初始化时清除所有LED
    ws2812_led_clear_all();
    
    ESP_LOGI(TAG, "WS2812 LED初始化完成");
    return ESP_OK;
}

/**
 * @brief 设置单个LED的颜色（立即生效）
 */
esp_err_t ws2812_led_set_color(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "WS2812 LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (led_index >= led_count) {
        ESP_LOGE(TAG, "LED索引超出范围: %d >= %d", led_index, led_count);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = led_strip_set_pixel(led_strip, led_index, red, green, blue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置LED %d颜色失败: %s", led_index, esp_err_to_name(ret));
        return ret;
    }

    // 立即刷新显示
    ret = led_strip_refresh(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "刷新LED显示失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "设置LED %d颜色: R=%d, G=%d, B=%d", led_index, red, green, blue);
    return ESP_OK;
}

/**
 * @brief 设置所有LED为相同颜色（立即生效）
 */
esp_err_t ws2812_led_set_all_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "WS2812 LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    for (uint16_t i = 0; i < led_count; i++) {
        esp_err_t ret = led_strip_set_pixel(led_strip, i, red, green, blue);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置LED %d颜色失败: %s", i, esp_err_to_name(ret));
            return ret;
        }
    }

    // 立即刷新显示
    esp_err_t ret = led_strip_refresh(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "刷新LED显示失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "设置所有LED颜色: R=%d, G=%d, B=%d", red, green, blue);
    return ESP_OK;
}

/**
 * @brief 清除所有LED（关闭所有LED，立即生效）
 */
esp_err_t ws2812_led_clear_all(void)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "WS2812 LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = led_strip_clear(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "清除LED失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 立即刷新显示
    ret = led_strip_refresh(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "刷新LED显示失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "清除所有LED");
    return ESP_OK;
}

/**
 * @brief 反初始化WS2812 LED组件
 */
esp_err_t ws2812_led_deinit(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "WS2812 LED未初始化");
        return ESP_OK;
    }

    esp_err_t ret = led_strip_del(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "删除LED strip设备失败: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip = NULL;
    led_count = 0;
    is_initialized = false;
    
    ESP_LOGI(TAG, "WS2812 LED反初始化完成");
    return ESP_OK;
}