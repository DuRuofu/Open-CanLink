#ifndef WS2812_LED_H
#define WS2812_LED_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WS2812 LED组件初始化
 * 
 * @param gpio_num GPIO引脚号
 * @param led_count LED数量
 * @return esp_err_t 
 *         - ESP_OK: 初始化成功
 *         - ESP_FAIL: 初始化失败
 */
esp_err_t ws2812_led_init(uint8_t gpio_num, uint16_t led_count);

/**
 * @brief 设置单个LED的颜色（立即生效）
 * 
 * @param led_index LED索引 (0-3)
 * @param red 红色分量 (0-255)
 * @param green 绿色分量 (0-255)
 * @param blue 蓝色分量 (0-255)
 * @return esp_err_t 
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t ws2812_led_set_color(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief 设置所有LED为相同颜色（立即生效）
 * 
 * @param red 红色分量 (0-255)
 * @param green 绿色分量 (0-255)
 * @param blue 蓝色分量 (0-255)
 * @return esp_err_t 
 *         - ESP_OK: 设置成功
 *         - ESP_FAIL: 设置失败
 */
esp_err_t ws2812_led_set_all_color(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief 清除所有LED（关闭所有LED，立即生效）
 * 
 * @return esp_err_t 
 *         - ESP_OK: 清除成功
 *         - ESP_FAIL: 清除失败
 */
esp_err_t ws2812_led_clear_all(void);

/**
 * @brief 反初始化WS2812 LED组件
 * 
 * @return esp_err_t 
 *         - ESP_OK: 反初始化成功
 *         - ESP_FAIL: 反初始化失败
 */
esp_err_t ws2812_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WS2812_LED_H