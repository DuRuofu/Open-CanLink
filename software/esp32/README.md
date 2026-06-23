# ESP32 固件

esp-can-link 的 ESP32-S3 固件代码。

## 目录结构

```
software/esp32/
├── components/              # 共享 ESP-IDF 组件
│   ├── can_driver/          # TWAI 驱动封装
│   ├── usb_cdc/             # USB CDC ACM 封装
│   └── protocol/            # JSON 协议解析器
├── test/                    # 硬件模块测试
│   ├── twai_loopback/       # CAN 芯片测试
│   └── usb_cdc_echo/        # USB 串口测试
├── can_bridge/              # 综合桥接程序（生产固件）
├── ref/                     # 参考代码（不参与编译）
└── README.md                # 本文件
```

## 构建

```bash
# 在 ESP-IDF 6.0 环境中，选择要构建的程序：

cd test/twai_loopback     # CAN 测试
cd test/usb_cdc_echo      # USB 测试
cd can_bridge             # 桥接程序

# 首次构建
idf.py set-target esp32s3
idf.py build

# 后续构建
idf.py build

# 烧录并查看日志
idf.py -p /dev/cu.usbmodem* flash monitor
```

## 构建顺序建议

1. `test/twai_loopback` — 先验证硬件
2. `test/usb_cdc_echo` — 验证通信通道
3. `can_bridge` — 集成所有功能

## 引脚配置

| 信号 | GPIO |
|------|------|
| CAN TX | GPIO4 |
| CAN RX | GPIO5 |
| USB DP | GPIO20 |
| USB DM | GPIO19 |

详见 [docs/firmware.md](../../docs/firmware.md)
