#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ws2812_led.h"
#include "sdkconfig.h"

static const char *TAG = "ws2812_demo";

#define BLINK_GPIO 46
#define BLINK_PERIOD 1000
#define LED_COUNT 4

static uint8_t s_led_state = 0;

/**
 * @brief LED闪烁控制函数
 */
static void blink_led(void)
{
    if (s_led_state)
    {
        // 设置四个LED为不同颜色（立即生效）
        ws2812_led_set_color(0, 50, 0, 0);   // LED0: 红色
        ws2812_led_set_color(1, 0, 50, 0);   // LED1: 绿色
        ws2812_led_set_color(2, 0, 0, 50);   // LED2: 蓝色
        ws2812_led_set_color(3, 50, 50, 0);  // LED3: 黄色
    }
    else
    {
        ws2812_led_clear_all(); // 立即生效
    }
}

/**
 * @brief 应用程序主函数
 */
void app_main(void)
{
    // 初始化WS2812 LED组件
    ESP_ERROR_CHECK(ws2812_led_init(BLINK_GPIO, LED_COUNT));
    
    while (1)
    {
        ESP_LOGI(TAG, "LED 状态: %s", s_led_state ? "ON" : "OFF");
        blink_led();
        s_led_state = !s_led_state;
        vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
