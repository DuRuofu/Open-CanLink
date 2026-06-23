# esp-can-link 快速上手指南

## 准备工作

### 硬件

- ESP32-S3 开发板
- TJA1051T/3,118 CAN 收发器模块
- USB-C 数据线
- 120Ω 终端电阻（CAN 总线两端各一个）

### 软件

- ESP-IDF 6.0.1（通过 `get_idf60` 激活）
- Python 3.12+（PC 上位机）
- `uv` 包管理器

## 第一步：验证 CAN 硬件

烧录 `twai_loopback` 测试程序，验证 ESP32-S3 TWAI 控制器和 TJA1051 连接。

```bash
cd software/esp32/test/twai_loopback
get_idf60
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

观察串口输出，确认所有比特率测试 PASS。

## 第二步：验证 USB 串口

烧录 `usb_cdc_echo` 测试程序，验证 USB CDC 虚拟串口。

```bash
cd software/esp32/test/usb_cdc_echo
get_idf60
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

用串口工具（如 screen、minicom 或 PC 上位机）连接，输入字符确认回显。

## 第三步：烧录桥接固件

```bash
cd software/esp32/can_bridge
get_idf60
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

## 第四步：启动 PC 上位机

```bash
cd software/pc
uv sync
uv run python main.py
```

1. 点击「刷新」扫描串口
2. 选择 ESP32 对应的串口（如 `/dev/cu.usbmodem*` 或 `COMx`）
3. 点击「连接」
4. 切换到「CAN 发送」标签页
5. 点击「CAN Start」启动 CAN 接口
6. 输入 CAN ID 和数据，点击「发送一次」
7. 切换到「CAN 监视器」查看接收到的 CAN 帧

## 快速测试

发送设备信息查询:
```json
{"cmd":"get_info"}
```

预期响应:
```json
{"type":"info","firmware":"esp-can-link","version":"0.1.0","hw":"ESP32-S3+TJA1051"}
```

发送一个 CAN 帧:
```json
{"cmd":"send","id":291,"ext":false,"data":[1,2,3,4]}
```

## 故障排查

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| CAN 测试失败 | TJA1051 未连接 | 检查 TX→TXD, RX→RXD 连线 |
| USB 不识别 | TinyUSB 未启用 | 检查 `sdkconfig.defaults` |
| 串口连接失败 | 驱动问题 | macOS: 无需驱动; Windows: 安装 CP210x 驱动 |
| 无 CAN 数据 | CAN 未启动 | 发送 `{"cmd":"can_start"}` |
| Bus-off 错误 | 终端电阻缺失 | 确保 CAN 总线两端有 120Ω 电阻 |
