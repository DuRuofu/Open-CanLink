/*
 * Main.c - open-can-link CH32 USB-CAN Bridge Firmware
 *
 * USB CDC <=> CAN Bridge with JSON protocol.
 * Architecture: bare-metal super loop + ISR callbacks.
 *
 * Hardware:
 *   CH32V203C8T6 @ 144MHz
 *   CAN1 Remap1: PB9=TX, PB8=RX, PB15=STB (TJA1051)
 *   USB CDC: PA11=D-, PA12=D+ (WCH USBLIB)
 *   Debug UART: PA9=TX (USART1, 115200)
 *   WS2812 LED: PA7 (SPI1 MOSI + DMA)
 */

#include "debug.h"
#include "ch32v20x.h"

/* USB library */
#include "usb_lib.h"
#include "usb_desc.h"
#include "hw_config.h"
#include "usb_pwr.h"
#include "usb_prop.h"

/* Application modules */
#include "can_driver.h"
#include "protocol.h"
#include "command_handler.h"
#include "ws2812.h"
#include "ring_buffer.h"

/* ── USB CDC globals ── */

ring_buffer_t g_usb_rx_ring;               /* USB EP2 → main loop */
extern volatile uint8_t USBD_Endp3_Busy;   /* from usb_endp.c */
extern int usb_cdc_send(const uint8_t *data, uint16_t len);

/* ── System tick (incremented by TIM2 ISR every 1ms) ── */

volatile uint32_t g_sys_tick_ms = 0;

/* ── Forward declarations ── */

static void send_json_line(const char *line);
static void tim2_init(void);

/* ════════════════════════════════════════════════════════════════════ */

int main(void)
{
    /* ── 1. Core setup ── */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    printf("\r\n");
    printf("=============================================\r\n");
    printf("  open-can-link Bridge Firmware v0.1.0\r\n");
    printf("  HW: CH32V203C8T6 + TJA1051T\r\n");
    printf("  F_CPU: %u Hz\r\n", (unsigned int)SystemCoreClock);
    printf("=============================================\r\n");

    /* ── 2. Peripherals ── */
    ring_buffer_init(&g_usb_rx_ring);

    ws2812_init();
    ws2812_set_color(WS2812_GREEN >> 16, (WS2812_GREEN >> 8) & 0xFF, WS2812_GREEN & 0xFF);
    /* Wait — actually use the macro: ws2812_set_packed */
    ws2812_set_packed(WS2812_BLUE);
    ws2812_update();

    /* CAN: PB9=TX, PB8=RX, PB15=STB, 500kbps default */
    can_driver_init(GPIO_Pin_9, GPIO_Pin_8, GPIO_Pin_15, 500000);
    command_handler_init();

    /* ── 3. System tick (100μs → 1ms) ── */
    tim2_init();

    /* ── 4. USB CDC ── */
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    printf("Bridge ready. CAN stopped.\r\n");
    printf("Send {\"cmd\":\"can_start\"} to begin.\r\n");

    /* ════════════════════════════════════════════════════════════════
     * Main Loop
     * ════════════════════════════════════════════════════════════════ */

    uint32_t last_periodic = 0;
    uint32_t last_status   = 0;

    while (1)
    {
        /*
         * A. CAN → USB
         *    Poll CAN1 FIFO0. On frame received, build JSON and
         *    send to host via USB CDC bulk-IN endpoint.
         */
        {
            uint32_t can_id;
            bool     can_ext;
            uint8_t  can_dlc;
            uint8_t  can_data[8];
            uint32_t can_ts;

            if (can_driver_poll_receive(&can_id, &can_ext, &can_dlc,
                                         can_data, &can_ts)) {
                char json_line[256];
                uint32_t ts_ms = g_sys_tick_ms;
                int len = protocol_build_rx_frame(can_id, can_ext, can_dlc,
                                                   can_data, ts_ms,
                                                   json_line, sizeof(json_line));
                if (len > 0) {
                    send_json_line(json_line);
                }

                /* Quick blink: set green briefly */
                ws2812_set_packed(WS2812_GREEN);
                ws2812_update();
            }
        }

        /*
         * B. USB → CAN
         *    Drain ring buffer filled by EP2_OUT_Callback ISR.
         *    Feed bytes to protocol line parser, process complete
         *    JSON commands.
         */
        {
            uint8_t byte;
            while (ring_buffer_read(&g_usb_rx_ring, &byte)) {
                protocol_feed_byte((char)byte);
            }

            char line[512];
            while (protocol_get_line(line, sizeof(line)) > 0) {
                printf("CMD: %s\r\n", line);

                parsed_cmd_t cmd;
                if (protocol_parse(line, &cmd)) {
                    char response[256];
                    int rlen = command_handler_process(&cmd, response,
                                                        sizeof(response));
                    if (rlen > 0) {
                        send_json_line(response);
                    }
                } else {
                    /* Invalid JSON */
                    char err_resp[128];
                    int rlen = protocol_build_response(NULL, false,
                                "Invalid JSON", err_resp, sizeof(err_resp));
                    if (rlen > 0) send_json_line(err_resp);
                }
            }
        }

        /*
         * C. Periodic CAN transmit (every 10ms)
         */
        if (g_sys_tick_ms - last_periodic >= 10) {
            command_handler_periodic_tick();
            last_periodic = g_sys_tick_ms;
        }

        /*
         * D. Status LED refresh (every 2s)
         */
        if (g_sys_tick_ms - last_status >= 2000) {
            can_status_t st;
            can_driver_get_status(&st);

            switch (st.state) {
            case CAN_STATE_RUNNING:
                ws2812_set_packed(WS2812_GREEN);
                break;
            case CAN_STATE_BUS_OFF:
                ws2812_set_packed(WS2812_RED);
                break;
            default:
                ws2812_set_packed(WS2812_BLUE);
                break;
            }
            ws2812_update();

            printf("STATUS: state=%d tx_err=%u rx_err=%u\r\n",
                   st.state, st.tx_errors, st.rx_errors);
            last_status = g_sys_tick_ms;
        }
    }
}

/* ── JSON line sender ── */

static void send_json_line(const char *line)
{
    size_t len = strlen(line);
    if (len == 0) return;

    /* Append CR LF */
    char buf[512];
    size_t total = len;
    if (total > sizeof(buf) - 2) total = sizeof(buf) - 2;
    memcpy(buf, line, total);
    buf[total]     = '\r';
    buf[total + 1] = '\n';
    total += 2;

    usb_cdc_send((const uint8_t *)buf, (uint16_t)total);
}

/* ── TIM2: 100μs tick ── */

static void tim2_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitTypeDef tim = {0};
    /* APB1 = 72MHz. Timer clock = 72MHz (since APB1 prescaler ≠ 1, timer clock = 2×APB1) */
    /* Wait — on CH32V203, when APB1 prescaler ≠ 1, the timer clock is doubled. */
    /* PCLK1 = HCLK/2 = 72MHz. → Timer clock = 144MHz. */
    /* 100μs: prescaler=14 → 144MHz/15 = 9.6MHz; period=960 → 9.6MHz/960 = 100μs */
    /* Actually let me use simpler: prescaler=143 → 144MHz/144 = 1MHz; period=100 → 1MHz/100 = 10kHz → 100μs */
    tim.TIM_Period        = 100 - 1;         /* 99 → 100 counts */
    tim.TIM_Prescaler     = 144 - 1;         /* 143 → 144 divider */
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel    = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM2, ENABLE);
}
