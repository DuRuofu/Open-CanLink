# esp-can-link 固件开发指南

## 环境准备

ESP-IDF 6.0.1，目标芯片 ESP32-S3。

## 固件架构

```
software/esp32/
├── components/              # 共享组件库
│   ├── can_driver/          # TWAI 驱动封装
│   ├── usb_cdc/             # USB CDC ACM 封装
│   └── protocol/            # JSON 协议解析器
├── test/                    # 硬件模块测试
│   ├── twai_loopback/       # CAN 芯片测试
│   └── usb_cdc_echo/        # USB 串口测试
└── can_bridge/              # 综合桥接程序
```

## 三个程序

### 1. twai_loopback — CAN 芯片测试

验证 TWAI 控制器和 TJA1051 收发器硬件连接。

- **模式**: 内部自测试回环（无需外部 CAN 总线）
- **测试内容**: 标准帧/扩展帧/多比特率/突发发送
- **引脚**: TX=GPIO4, RX=GPIO5

```bash
cd software/esp32/test/twai_loopback
idf.py set-target esp32s3   # 首次
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

### 2. usb_cdc_echo — USB 串口测试

验证 ESP32-S3 USB-OTG CDC ACM 功能。

- **功能**: 回显所有接收到的数据
- **验证**: 用 PC 串口工具连接，输入字符，确认回显
- **引脚**: DP=GPIO20, DM=GPIO19 (固定)

```bash
cd software/esp32/test/usb_cdc_echo
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

### 3. can_bridge — 综合桥接程序

USB CDC ↔ CAN 桥接，实现完整 JSON 协议。

- **任务架构**:
  - `bridge` — USB RX → 协议解析 → 命令处理 → CAN TX
  - `periodic` — 周期 CAN 发送
  - `status_rpt` — 定期状态报告
  - `usb_cdc_rx` — USB 数据接收（组件内部）
  - `can_rx` — CAN 帧接收（组件内部）

```bash
cd software/esp32/can_bridge
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

## 引脚定义

| 信号 | GPIO | 连接 |
|------|------|------|
| CAN TX | GPIO4 | → TJA1051 TXD |
| CAN RX | GPIO5 | ← TJA1051 RXD |
| USB DP | GPIO20 | USB OTG (固定) |
| USB DM | GPIO19 | USB OTG (固定) |

## 构建系统

### 测试程序 (test/)

独立 ESP-IDF 项目，单文件 `main.c`，不依赖 `components/`。

最小 `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(项目名)
```

### 桥接程序 (can_bridge/)

使用 `EXTRA_COMPONENT_DIRS` 引用共享组件:
```cmake
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)
```

### 组件 (components/)

每个组件有标准 ESP-IDF 组件结构:
```
component_name/
├── CMakeLists.txt      # idf_component_register()
├── idf_component.yml   # 外部依赖（如 esp_tinyusb）
├── component_name.c    # 实现
└── include/
    └── component_name.h  # 公共 API
```

## sdkconfig.defaults

| 配置项 | 值 | 说明 |
|--------|-----|------|
| CONFIG_TINYUSB_CDC_ENABLED | y | 启用 USB CDC |
| CONFIG_TINYUSB_DESC_USE_ESPRESSIF_VID | y | 使用 Espressif VID/PID |
| CONFIG_TWAI_ISR_IN_IRAM | y | TWAI ISR 放在 IRAM |

## 调试

- **UART0**: 所有 `ESP_LOGI`/`ESP_LOGW` 输出到 UART0 串口
- **USB CDC**: 桥接程序的命令/数据通道
- **逻辑分析仪**: 可接 CANH/CANL 观察 CAN 总线波形

## 协议

详见 [docs/protocol.md](../../docs/protocol.md)。设备上电后 CAN 默认停止，需要发送 `{"cmd":"can_start"}` 启动。
