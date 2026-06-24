# CH32V203 USB-CAN 桥接固件设计

**日期**: 2026-06-24  
**目标芯片**: CH32V203C8T6 (RISC-V, 144MHz, 64KB Flash / 20KB RAM)  
**开发环境**: MounRiver Studio Ⅱ

---

## 1. 概述

为 open-can-link 项目新增方案 B 固件，在 CH32V203C8T6 上实现 USB CDC ACM ↔ CAN 2.0 桥接，
与现有 ESP32-S3 方案 A 共用同一套 JSON 协议和 PC 上位机。

## 2. 架构决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 调度模型 | 裸机超级循环 | 144MHz 够用，WCH 参考代码均为裸机，20KB RAM 不适配 RTOS |
| 项目起点 | SimulateCDC 例程改造 | USB 库配置繁琐，从模板改避免底层踩坑 |
| CAN RX 触发 | 主循环轮询 | CAN1 与 USB 共享中断向量，轮询避免 ISR 冲突 |
| 协议层 | 直接复制 ESP32 的 protocol.c/h | 纯 C 零依赖，无需修改 |

## 3. 硬件引脚

| 功能 | 引脚 | 外设 |
|------|------|------|
| CAN_TX | PB9 | CAN1 (Remap1) |
| CAN_RX | PB8 | CAN1 (Remap1) |
| CAN_S | PB15 | GPIO 推挽输出 |
| USB_D- | PA11 | USB (内置) |
| USB_D+ | PA12 | USB (内置) |
| UART_TX (调试) | PA9 | USART1 |
| UART_RX (调试) | PA10 | USART1 |
| WS2812 | PA7 | SPI1 MOSI (DMA) |

## 4. 数据流

```
CAN 总线 → CAN1 (轮询RX) → protocol_build_rx_frame → JSON → USB CDC EP3 IN → 上位机
USB CDC EP2 OUT → 环形缓冲 → protocol_feed_byte → 命令解析 → CAN_Transmit → CAN 总线
WS2812 PA7 → SPI+DMA → 状态指示 (蓝=就绪, 绿=运行, 红=错误)
```

## 5. 文件结构

```
software/ch32/can_bridge/
├── User/
│   ├── Main.c                     # 入口 + 主循环
│   ├── can_driver.c / can_driver.h      # CAN 驱动封装
│   ├── command_handler.c / command_handler.h  # 命令处理
│   ├── protocol.c / protocol.h           # 从 ESP32 复制，一字不改
│   ├── ws2812.c / ws2812.h               # WS2812 SPI+DMA 驱动
│   ├── ring_buffer.c / ring_buffer.h     # 环形缓冲区
│   ├── ch32v20x_conf.h                   # 芯片外设使能
│   ├── ch32v20x_it.c / ch32v20x_it.h     # 中断服务
│   ├── system_ch32v20x.c / system_ch32v20x.h  # 系统时钟
│   └── debug.h / debug.c                 # 调试打印
├── USBLIB/
│   ├── CONFIG/
│   │   ├── usb_desc.c / usb_desc.h       # USB 描述符 (VID/PID)
│   │   ├── usb_endp.c                    # EP2_OUT / EP3_IN 回调
│   │   ├── usb_prop.c / usb_prop.h       # CDC 类请求
│   │   ├── usb_pwr.c / usb_pwr.h         # 电源管理
│   │   └── hw_config.c / hw_config.h     # USB 时钟 + 中断配置
│   └── USB-Driver/                        # 底层栈 (不改)
└── (MRS 项目文件)
```

## 6. 模块设计

### 6.1 Main.c

```
INIT:
  1. NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1)
  2. 系统时钟 144MHz (HSE 8MHz × PLL)
  3. Delay_Init()
  4. USART_Printf_Init(115200)    调试串口
  5. can_driver_init()            CAN 初始化，默认 stop 状态
  6. ws2812_init()                WS2812 初始化
  7. ws2812_set(蓝)               就绪指示
  8. ring_buffer_init()           接收缓冲
  9. command_handler_init()        命令处理
  10. Set_USBConfig()             USB 时钟
  11. USB_Init() + USB_Interrupts_Config()
  12. 启动 CAN RX 任务: can_driver_start_rx()  (可选)

MAIN LOOP:
  while(1) {
    // A. CAN → USB (每轮检查)
    can_rx = can_driver_poll_receive();
    if (can_rx.received) {
      protocol_build_rx_frame(id, ext, dlc, data, tick, json_buf);
      usb_cdc_send(json_buf, len);
      ws2812_blip();
    }

    // B. USB → CAN (处理环形缓冲中所有可用数据)
    uint8_t byte;
    while (ring_buffer_read(&byte)) {
      protocol_feed_byte(byte);
      if (protocol_get_line(line)) {
        if (protocol_parse(line, &cmd)) {
          command_handler_process(&cmd, response);
          usb_cdc_send(response);
          ws2812_blip();
        }
      }
    }

    // C. 周期任务 (每 10ms)
    if (tick - last_periodic >= 10) {
      command_handler_periodic_tick();  // 处理周期 CAN 发送
      last_periodic = tick;
    }

    // D. 状态刷新 (每 2s)
    if (tick - last_status >= 2000) {
      can_driver_get_status(&status);
      if (status.state == RUNNING)  ws2812_set(绿);
      else if (status.bus_off)      ws2812_set(红);
      else                          ws2812_set(蓝);
      last_status = tick;
    }
  }
```

### 6.2 CAN 驱动 (can_driver.c)

封装 WCH CAN1 外设的 STM32 风格 API。

**公开接口:**

```c
esp_err_t can_driver_init(uint8_t tx_pin, uint8_t rx_pin, uint8_t standby_pin, uint32_t bitrate);
esp_err_t can_driver_start(void);
esp_err_t can_driver_stop(void);
esp_err_t can_driver_send(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data);
esp_err_t can_driver_set_bitrate(uint32_t bitrate);
esp_err_t can_driver_set_filter(const filter_entry_t *filters, size_t count);
esp_err_t can_driver_get_status(can_status_t *status);
```

**波特率计算** (144MHz, APB1=72MHz):

| 目标 | Prescaler | BS1 | BS2 | SJW | 实际 |
|------|-----------|-----|-----|-----|------|
| 125k | 36 | 12 | 3 | 1 | 125k |
| 250k | 18 | 12 | 3 | 1 | 250k |
| 500k | 9 | 12 | 3 | 1 | 500k |
| 1M | 9 | 5 | 2 | 1 | 1M |

**初始化**: GPIO 配置 → CAN1 时钟 → `CAN_Init()` → `CAN_FilterInit()` (全通模式 by default)

**发送**: `CAN_Transmit(CAN1, &tx_msg)` 阻塞等待完成（超时保护）

**接收**: 主循环通过 `CAN_MessagePending(CAN1, CAN_FIFO0)` + `CAN_Receive()` 轮询

**状态**: 读 CAN_ESR 寄存器，判断 BOFF/EPVF/EWGF 标志位，错误计数器

### 6.3 USB CDC (基于 USBLIB 修改)

从 SimulateCDC 复制完整 USBLIB，修改以下文件:

**usb_desc.c**: 改 VID/PID 和字符串描述符
- VID = 0xCAFE, PID = 0x0001 (临时，正式发布前申请)
- Product String = "open-can-link CH32 CAN Bridge"

**usb_endp.c**: 修改 EP2_OUT_Callback 和 EP3_IN_Callback
- EP2_OUT: 收到的 USB 数据写入环形缓冲 (不是原例程的 UART)
- EP3_IN: 回调中清 busy 标志
- 提供 `usb_cdc_send(data, len)` — 非阻塞发送，内部分 64 字节包

**usb_prop.c**: 删掉 UART 相关代码 (原来 SET_LINE_CODING 会重新初始化 UART)

**hw_config.c**: 保留 USB 中断配置，删 UART2 相关

### 6.4 环形缓冲 (ring_buffer.c)

USB EP2_OUT ISR (生产者) → 主循环 (消费者)，标准无锁设计:

```c
typedef struct {
    uint8_t buf[4096];
    volatile uint16_t head;   // ISR 写
    uint16_t tail;            // 主循环读
} ring_buffer_t;

void ring_buffer_write_isr(ring_buffer_t *rb, uint8_t byte);  // ISR 调用
bool ring_buffer_read(ring_buffer_t *rb, uint8_t *byte);       // 主循环调用
```

### 6.5 命令处理 (command_handler.c)

从 ESP32 版本移植，适配 CAN 驱动接口。支持全部 9 条命令：

| 命令 | 实现 |
|------|------|
| can_start | can_driver_start() |
| can_stop | can_driver_stop() |
| set_bitrate | can_driver_set_bitrate() |
| set_filter | can_driver_set_filter() |
| send | can_driver_send() |
| periodic_start | 注册到周期性任务列表 (最多4条) |
| periodic_stop | 从任务列表移除 |
| get_status | can_driver_get_status() → protocol_build_status() |
| get_info | 返回 "open-can-link" / "CH32V203+TJA1051T" |

### 6.6 WS2812 (ws2812.c)

从 `APPLICATION/WS2812_LED` 参考代码的 SPI+DMA 模式移植:
- PA7 = SPI1_MOSI, 3MHz 时钟
- 颜色顺序: G-R-B (与参考代码一致)
- 单像素场景 (硬件只有一颗 LED)
- `ws2812_set(r, g, b)` → 更新 buffer → `w2812_sync()` 触发 DMA
- 3 个预设: BLUE(0,0,20) / GREEN(20,0,0) / RED(0,20,0)  (GRB 编码! 注意 ESP32 是 RGB)
- `ws2812_blip()`: 短暂闪烁(100ms)后恢复原状态

### 6.7 协议层 (protocol.c/h)

从 `software/esp32/components/protocol/` **完全复制**，不改一行。

## 7. 内存预算

| 项目 | 大小 |
|------|------|
| USB PMA 缓冲区 | ~400B |
| USB 环形缓冲 | 4096B |
| protocol 行缓冲 | 512B |
| JSON 构建缓冲 | 256B × 2 |
| WS2812 DMA 缓冲 | ~64B |
| 堆栈 + 全局 | ~2KB |
| WCH USB 库 static | ~2KB |
| **合计** | **~9.5KB** |

20KB RAM 中剩余 ~10KB 余量，安全。

## 8. 与 ESP32 方案对比

| 项目 | ESP32-S3 | CH32V203 |
|------|----------|----------|
| USB CDC | TinyUSB | WCH USBLIB |
| CAN 驱动 | TWAI (ESP-IDF) | CAN1 (STM32-style) |
| 调度 | FreeRTOS 任务 | 裸机主循环 |
| 指示灯 | GPIO21 RMT WS2812 | PA7 SPI+DMA WS2812 |
| 调试输出 | 通过 USB CDC | 独立 UART1 (PA9/PA10) |
| 协议实现 | protocol.c (同) | protocol.c (同) |

## 9. 实现顺序

1. **搭建项目骨架** — MRS 项目创建，复制 USBLIB + protocol
2. **USB CDC 调通** — 修改 usb_endp.c，USART1 打印 USB 收发内容
3. **CAN 驱动实现** — 用 TWAI loopback 验证 CAN 收发
4. **桥接逻辑** — 主循环 USB↔CAN 桥接，上位机测试
5. **命令处理** — command_handler 全部命令
6. **WS2812** — LED 状态指示
7. **完整测试** — 全部 9 条命令 + 收发包，与 PC 上位机联调
