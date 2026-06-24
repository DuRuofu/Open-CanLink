# CH32V203 USB-CAN 桥接固件 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 CH32V203C8T6 上实现完整 USB CDC ↔ CAN 桥接固件，与现有 PC 上位机兼容。

**Architecture:** 裸机超级循环，从 WCH SimulateCDC 例程改造。USB EP2 OUT 回调 → 环形缓冲 → protocol 解析 → CAN 发送。CAN 主循环轮询 → protocol 格式化 → USB EP3 IN 上传。WS2812 SPI+DMA 状态指示。

**Tech Stack:** MounRiver Studio Ⅱ, WCH HAL (ch32v20x 标准外设库), WCH USBLIB (USB CDC), RISC-V GCC

**Design Spec:** `docs/superpowers/specs/2026-06-24-ch32-can-bridge-design.md`

## Global Constraints

- 裸机，不使用 FreeRTOS
- protocol.c/h 从 ESP32 版本一字不改复制
- USB CDC 使用 WCH USBLIB (SimulateCDC 为模板)
- CAN 使用 STM32 风格 API (ch32v20x_can.h)
- WS2812 使用 SPI+DMA (PA7)
- 所有 9 条 JSON 命令必须支持
- 与现有 PC 上位机完全兼容

---

## File Structure Map

```
software/ch32/can_bridge/                   ← 项目根 (MRS 项目目录)
├── User/                                   ← 应用代码
│   ├── Main.c                              ← T8: 主循环
│   ├── can_driver.c / can_driver.h         ← T5: CAN1 驱动封装
│   ├── command_handler.c / command_handler.h ← T7: 命令处理
│   ├── protocol.c / protocol.h             ← T2: 从ESP32复制
│   ├── ring_buffer.c / ring_buffer.h       ← T3: 环形缓冲
│   ├── ws2812.c / ws2812.h                 ← T6: WS2812 SPI+DMA
│   ├── ch32v20x_conf.h                     ← T1: 外设使能
│   ├── ch32v20x_it.c / ch32v20x_it.h       ← T1: 中断服务
│   ├── system_ch32v20x.c / system_ch32v20x.h ← T1: 时钟初始化
│   └── debug.h / debug.c                   ← T1: printf→USART1
├── USBLIB/                                 ← T4: SimulateCDC改造
│   ├── CONFIG/
│   │   ├── usb_desc.c/h    ← T4: 改VID/PID/描述符
│   │   ├── usb_endp.c      ← T4: EP2/EP3回调→环形缓冲
│   │   ├── usb_prop.c/h    ← T4: 删UART
│   │   ├── hw_config.c/h   ← T4: 删UART,留USB
│   │   ├── usb_pwr.c/h, usb_conf.h, usb_istr.c/h ← 不改
│   └── USB-Driver/         ← 完整从 SimulateCDC 复制，不改
```

---

### Task 1: 搭建项目骨架

**Files:**
- Copy: `software/ch32/ref/USB/USBD/SimulateCDC/*` → `software/ch32/can_bridge/`
- Copy: `software/ch32/ref/SRC/Startup/startup_ch32v20x_D8.S` → `software/ch32/can_bridge/User/`
- Copy: `software/ch32/ref/SRC/Core/core_riscv.h` → `software/ch32/can_bridge/User/`
- Modify: `software/ch32/can_bridge/User/ch32v20x_conf.h`
- Modify: `software/ch32/can_bridge/User/debug.h`
- Modify: `software/ch32/can_bridge/User/debug.c`
- Delete: `software/ch32/can_bridge/User/UART/`

- [ ] **Step 1: Copy SimulateCDC as template**

```bash
cp -r software/ch32/ref/USB/USBD/SimulateCDC/* software/ch32/can_bridge/
cp software/ch32/ref/SRC/Startup/startup_ch32v20x_D8.S software/ch32/can_bridge/User/
cp software/ch32/ref/SRC/Core/core_riscv.h software/ch32/can_bridge/User/
rm -rf software/ch32/can_bridge/User/UART/
```

- [ ] **Step 2: ch32v20x_conf.h — 使能所有需要的外设头文件**

```c
#ifndef __CH32V20x_CONF_H
#define __CH32V20x_CONF_H

#include "ch32v20x_adc.h"
#include "ch32v20x_bkp.h"
#include "ch32v20x_can.h"
#include "ch32v20x_crc.h"
#include "ch32v20x_dbgmcu.h"
#include "ch32v20x_dma.h"
#include "ch32v20x_exti.h"
#include "ch32v20x_flash.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_i2c.h"
#include "ch32v20x_iwdg.h"
#include "ch32v20x_misc.h"
#include "ch32v20x_pwr.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_rtc.h"
#include "ch32v20x_spi.h"
#include "ch32v20x_tim.h"
#include "ch32v20x_usart.h"
#include "ch32v20x_wwdg.h"

#endif
```

- [ ] **Step 3: debug.c — printf 重定向到 USART1 (PA9 TX)**

```c
/* debug.h */
#ifndef __DEBUG_H
#define __DEBUG_H
#include "ch32v20x.h"
#include <stdio.h>
void USART_Printf_Init(uint32_t baudrate);
#endif

/* debug.c */
#include "debug.h"

void USART_Printf_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

int _write(int fd, char *buf, int size)
{
    for (int i = 0; i < size; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, buf[i]);
    }
    return size;
}
```

- [ ] **Step 4: Main.c 骨架 — 删 UART 桥接逻辑，留空主循环**

```c
#include "debug.h"
#include "ch32v20x.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "hw_config.h"
#include "usb_pwr.h"
#include "usb_prop.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("open-can-link CH32 Bridge v0.1.0\r\n");

    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    while (1) { /* TBD in Task 8 */ }
}
```

- [ ] **Verify: Build in MounRiver Studio → 0 errors**

---

### Task 2: 复制 protocol.c/h

**Files:**
- Create: `software/ch32/can_bridge/User/protocol.c` (copy from ESP32)
- Create: `software/ch32/can_bridge/User/protocol.h` (copy from ESP32)

- [ ] **Step 1: Copy files**

```bash
cp software/esp32/components/protocol/protocol.c software/ch32/can_bridge/User/protocol.c
cp software/esp32/components/protocol/include/protocol.h software/ch32/can_bridge/User/protocol.h
```

- [ ] **Step 2: Add files to MRS project, rebuild → 0 errors**

---

### Task 3: 实现环形缓冲

**Files:**
- Create: `software/ch32/can_bridge/User/ring_buffer.h`
- Create: `software/ch32/can_bridge/User/ring_buffer.c`

- [ ] **ring_buffer.h**

```c
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define RING_BUFFER_SIZE 4096

typedef struct {
    uint8_t buf[RING_BUFFER_SIZE];
    volatile uint16_t head;   /* ISR writes */
    uint16_t tail;            /* main loop reads */
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *rb);
void ring_buffer_write_isr(ring_buffer_t *rb, uint8_t byte);
bool ring_buffer_read(ring_buffer_t *rb, uint8_t *byte);
uint16_t ring_buffer_available(ring_buffer_t *rb);

#endif
```

- [ ] **ring_buffer.c**

```c
#include "ring_buffer.h"

void ring_buffer_init(ring_buffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

void ring_buffer_write_isr(ring_buffer_t *rb, uint8_t byte)
{
    uint16_t next = (rb->head + 1) % RING_BUFFER_SIZE;
    if (next == rb->tail) {
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE; /* drop oldest */
    }
    rb->buf[rb->head] = byte;
    rb->head = next;
}

bool ring_buffer_read(ring_buffer_t *rb, uint8_t *byte)
{
    if (rb->tail == rb->head) return false;
    *byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    return true;
}

uint16_t ring_buffer_available(ring_buffer_t *rb)
{
    if (rb->head >= rb->tail) return rb->head - rb->tail;
    return RING_BUFFER_SIZE - rb->tail + rb->head;
}
```

- [ ] **Verify: Build → 0 errors**

---

### Task 4: 修改 USB CDC 配置 (USBLIB)

**Files:**
- Modify: `software/ch32/can_bridge/User/USBLIB/CONFIG/usb_desc.c`
- Modify: `software/ch32/can_bridge/User/USBLIB/CONFIG/usb_endp.c`
- Modify: `software/ch32/can_bridge/User/USBLIB/CONFIG/usb_prop.c`
- Modify: `software/ch32/can_bridge/User/USBLIB/CONFIG/hw_config.c`

Key changes from SimulateCDC:
1. **usb_desc.c**: Change product string to `"open-can-link CAN Bridge"`
2. **usb_endp.c**: EP2_OUT_Callback writes to ring_buffer instead of UART buffer; EP3_IN_Callback clears busy flag; add `usb_cdc_send()` public API
3. **usb_prop.c**: Remove `UART2_USB_Init()` call from `USBD_Status_In()`
4. **hw_config.c**: Remove USART2 clock enable and UART2 init; keep USB config

- [ ] **Step 1: usb_endp.c — add `#include "ring_buffer.h"`, declare extern ring buffer**

```c
#include "ring_buffer.h"

extern ring_buffer_t g_usb_rx_ring;
volatile uint8_t USBD_Endp3_Busy = 0;

void EP2_OUT_Callback(void)
{
    uint8_t buf[64];
    uint16_t len = USB_SIL_Read(EP2_OUT, buf);
    for (uint16_t i = 0; i < len; i++) {
        ring_buffer_write_isr(&g_usb_rx_ring, buf[i]);
    }
    SetEPRxStatus(ENDP2, EP_RX_VALID);
}

void EP3_IN_Callback(void)
{
    USBD_Endp3_Busy = 0;
}

int usb_cdc_send(const uint8_t *data, uint16_t len)
{
    if (len == 0) return 0;
    uint16_t remain = len;
    const uint8_t *ptr = data;
    while (remain > 0) {
        uint16_t chunk = (remain > 64) ? 64 : remain;
        if (USBD_Endp3_Busy) return -1;
        USBD_Endp3_Busy = 1;
        UserToPMABufferCopy((uint8_t *)ptr, ENDP3_TXADDR, chunk);
        SetEPTxCount(ENDP3, chunk);
        SetEPTxStatus(ENDP3, EP_TX_VALID);
        ptr += chunk;
        remain -= chunk;
    }
    return 0;
}
```

- [ ] **Step 2: usb_desc.c — change product string to `"open-can-link CAN Bridge"`**

Find the string descriptor array and update `Virtual_Com_Port_StringProduct`.

- [ ] **Step 3: usb_prop.c — delete `UART2_USB_Init()` in `USBD_Status_In()`**

```c
void USBD_Status_In(void)
{
    /* UART2_USB_Init() removed — we don't need line coding */
}
```

- [ ] **Step 4: hw_config.c — delete USART2 clock and UART init**

Remove `RCC_APB1Periph_USART2` from `RCC_Configuration()`. Keep `Set_USBConfig()` and `USB_Interrupts_Config()` intact.

- [ ] **Verify: Build → 0 errors. All USB library files link with ring_buffer.**

---

### Task 5: 实现 CAN 驱动

**Files:**
- Create: `software/ch32/can_bridge/User/can_driver.h`
- Create: `software/ch32/can_bridge/User/can_driver.c`

- [ ] **can_driver.h — full API**

```c
#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { CAN_STATE_STOPPED = 0, CAN_STATE_RUNNING, CAN_STATE_BUS_OFF } can_state_t;

typedef struct {
    can_state_t state;
    uint8_t tx_errors, rx_errors;
    bool bus_off;
} can_status_t;

typedef struct { uint32_t id, mask; bool ext; } can_filter_entry_t;

void can_driver_init(uint8_t tx_pin, uint8_t rx_pin, uint8_t stb_pin, uint32_t bitrate);
void can_driver_start(void);
void can_driver_stop(void);
bool can_driver_send(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data);
void can_driver_set_bitrate(uint32_t bitrate);
void can_driver_set_filter(const can_filter_entry_t *filters, uint8_t count);
void can_driver_get_status(can_status_t *status);
bool can_driver_poll_receive(uint32_t *id, bool *ext, uint8_t *dlc,
                              uint8_t *data, uint32_t *timestamp_us);
#endif
```

- [ ] **can_driver.c — core implementation (see spec for full details)**

Key functions to implement:
- `can_driver_init()`: GPIOB remap CAN1, CAN_Init (ABOM=ENABLE, NART=DISABLE), CAN_FilterInit (pass-all), STB pin LOW. Default stopped.
- `can_driver_start()`: Clear INRQ bit, set STB=LOW
- `can_driver_stop()`: Set INRQ bit, set STB=HIGH
- `can_driver_send()`: CAN_Transmit with timeout
- `can_driver_poll_receive()`: CAN_MessagePending + CAN_Receive
- `can_driver_set_bitrate()`: Write BTR register directly with prescaler/BS1/BS2 table (125k/250k/500k/1M, APB1=72MHz)
- `can_driver_set_filter()`: CAN_FilterInit for each rule (up to 14)
- `can_driver_get_status()`: Read ESR register (BOFF/TEC/REC)

Full implementation code is in the design spec `docs/superpowers/specs/2026-06-24-ch32-can-bridge-design.md` Section 6.2.

- [ ] **Verify: Build → 0 errors**

---

### Task 6: 实现 WS2812 驱动

**Files:**
- Create: `software/ch32/can_bridge/User/ws2812.h`
- Create: `software/ch32/can_bridge/User/ws2812.c`

- [ ] **ws2812.h**

```c
#ifndef WS2812_H
#define WS2812_H
#include <stdint.h>

/* GRB order! */
#define WS2812_COLOR_OFF     0, 0, 0
#define WS2812_COLOR_BLUE    0, 0, 20
#define WS2812_COLOR_GREEN   20, 0, 0
#define WS2812_COLOR_RED     0, 20, 0

void ws2812_init(void);
void ws2812_set_color(uint8_t g, uint8_t r, uint8_t b);
void ws2812_update(void);
#endif
```

- [ ] **ws2812.c — SPI+DMA on PA7**

Port from `software/ch32/ref/APPLICATION/WS2812_LED/User/main.c` SPI mode:
- SPI1, PA7 MOSI, 3MHz, CPOL=High, CPHA=1Edge
- Bit encoding: 1→0xE, 0→0x8 (4 SPI bits per WS2812 bit)
- Color order: G, R, B
- DMA1_Channel3, memory→peripheral, normal mode
- Single pixel buffer: 12 SPI bytes + 25 reset bytes = 37 bytes
- `ws2812_set_color()` updates buffer, `ws2812_update()` triggers DMA

Full implementation in design spec Section 6.6.

- [ ] **Verify: Build → 0 errors**

---

### Task 7: 实现命令处理

**Files:**
- Create: `software/ch32/can_bridge/User/command_handler.h`
- Create: `software/ch32/can_bridge/User/command_handler.c`

- [ ] **command_handler.h**

```c
#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

void command_handler_init(void);
int  command_handler_process(const parsed_cmd_t *cmd, char *out, size_t max_len);
void command_handler_periodic_tick(void);
#endif
```

- [ ] **command_handler.c — port from ESP32 version**

Implement all 9 commands:
1. `CMD_CAN_START` → `can_driver_start()` + ok response
2. `CMD_CAN_STOP` → `can_driver_stop()` + ok response
3. `CMD_SEND` → `can_driver_send()` + ok/error response
4. `CMD_PERIODIC_START` → register in slot array (max 4), record period_ms + data
5. `CMD_PERIODIC_STOP` → unregister from slot array by id
6. `CMD_SET_BITRATE` → `can_driver_set_bitrate()`
7. `CMD_SET_FILTER` → `can_driver_set_filter()`
8. `CMD_GET_STATUS` → `can_driver_get_status()` + `protocol_build_status()`
9. `CMD_GET_INFO` → `protocol_build_info("open-can-link", "0.1.0", "CH32V203+TJA1051T")`

`command_handler_periodic_tick()`: iterate slots, send frames when elapsed >= period_ms.

Full implementation in design spec Section 6.5.

- [ ] **Verify: Build → 0 errors**

---

### Task 8: 编写主循环 Main.c

**Files:**
- Modify: `software/ch32/can_bridge/User/Main.c`

- [ ] **Main.c — complete glue code**

Initialize order:
1. NVIC → SystemCoreClock → Delay → USART1 printf
2. ring_buffer, WS2812 (blue), CAN (stopped, 500k), command_handler
3. TIM2 (100μs tick → g_sys_tick_ms)
4. USB CDC (Set_USBConfig → USB_Init → USB_Interrupts_Config)

Main loop:
```
A. CAN → USB: poll_receive → protocol_build_rx_frame → send_json_line
B. USB → CAN: ring_buffer_read × N → protocol_feed_byte → protocol_get_line → parse → command_handler → send_json_line
C. 每10ms: command_handler_periodic_tick()
D. 每2s: get_status → update WS2812 color (blue/green/red)
```

`send_json_line()`: append `\r\n`, call `usb_cdc_send()`.

TIM2_IRQHandler: accumulate 100μs → increment g_sys_tick_ms.

Full implementation in design spec Section 6.1.

- [ ] **Verify: Build whole project → 0 errors, 0 warnings**

---

### Task 9: 烧录测试 + 上位机联调

- [ ] **Step 1: Flash firmware via WCH-Link or serial ISP**
- [ ] **Step 2: Connect hardware — TJA1051 CAN module + USB cable**
- [ ] **Step 3: Open PC上位机 `uv run python main.py`, select COM port, connect**
- [ ] **Step 4: Verify get_info** → `{"type":"info","firmware":"open-can-link","version":"0.1.0","hw":"CH32V203+TJA1051T"}`
- [ ] **Step 5: Verify LED states** — Blue (ready) → Green (after CAN Start) → Red (bus off)
- [ ] **Step 6: Verify CAN send** — `{"cmd":"send","id":0x123,"ext":false,"data":[1,2,3,4]}` → ok response
- [ ] **Step 7: Verify CAN receive** — another CAN node sends → frame appears in上位机
- [ ] **Step 8: Verify periodic send** — start/stop periodic frames
- [ ] **Step 9: Verify bitrate switching** — 125k/250k/500k/1M
- [ ] **Step 10: Verify filter** — set_filter → only matching IDs received
- [ ] **Step 11: Verify persistence** — save quick commands → restart上位机 → still there
- [ ] **Step 12: Verify CSV export**

---
