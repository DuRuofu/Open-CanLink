# esp-can-link

基于 **ESP32-S3 + TJA1051T/3,118** 的 USB / Wi-Fi 双通道 CAN 2.0 调试工具。

---

## 项目定位

做一个开放、可编程、低成本的 CAN 调试接口，替代封闭的厂商 USB-CAN 模块。

可通过以下方式控制 CAN 总线：

- USB CDC 虚拟串口
- Wi-Fi Web 页面 / WebSocket / TCP
- PC 上位机（PySide6 桌面应用）
- Python 脚本

面向日常产品开发、设备调试、现场测试场景，不替代专业汽车 CAN 分析仪。

---

## 项目结构

```
esp-can-link/
├── docs/                     # 项目文档
│   ├── protocol.md           # JSON 通信协议
│   ├── firmware.md            # 固件开发指南
│   ├── getting_started.md     # 快速上手
│   └── pc_upper_computer.md   # PC 上位机指南
├── software/
│   ├── esp32/                # ESP32-S3 固件
│   │   ├── components/       # 共享组件库
│   │   ├── test/             # 硬件模块测试
│   │   └── can_bridge/       # 综合桥接程序
│   ├── pc/                   # PC 上位机（PySide6）
│   └── ref/                  # 参考代码
├── hardware/                 # 硬件设计
├── web/                      # 网页调试界面
├── README.md
└── CLAUDE.md
```

---

## 硬件

| 模块 | 型号 |
|------|------|
| 主控 | ESP32-S3 |
| CAN 收发器 | TJA1051T/3,118（NXP，SOIC-8） |
| USB | USB OTG（DP: GPIO20, DM: GPIO19） |
| 无线 | Wi-Fi 2.4 GHz（AP / STA） |

### 引脚连接

```
ESP32-S3 CAN_TX (GPIO4)  →  TJA1051T/3,118 TXD
ESP32-S3 CAN_RX (GPIO5)  ←  TJA1051T/3,118 RXD
TJA1051T/3,118 CANH/CANL →  CAN 总线
```

> GPIO 不能直连 CANH/CANL，必须通过 CAN 收发器。CAN 总线需要 120Ω 终端电阻。

## 文档

| 文档 | 说明 |
|------|------|
| [docs/protocol.md](docs/protocol.md) | JSON 通信协议规范 |
| [docs/firmware.md](docs/firmware.md) | 固件开发指南 |
| [docs/getting_started.md](docs/getting_started.md) | 快速上手指南 |
| [docs/pc_upper_computer.md](docs/pc_upper_computer.md) | PC 上位机使用指南 |

---

---

## CAN 支持

Classical CAN 2.0A / 2.0B，11 位标准帧 + 29 位扩展帧，0~8 字节数据。

波特率：125k / 250k / 500k / 1M bps。

不支持 CAN FD / CAN XL。

---

## 工作模式

- **USB-CAN 有线模式**：USB 连接 PC，适合自动化测试和脚本控制
- **Wi-Fi 无线模式**：设备自建热点（默认 `ESP_CAN_LINK_xxxx`，`192.168.4.1`），网页调试
- **网关模式**：设备接入现有网络，提供 TCP / WebSocket 接口

---

## 通信协议

USB CDC 和 Wi-Fi 共用同一套 JSON 命令协议，详见 [docs/protocol.md](docs/protocol.md)。

### 设计哲学：为什么用 JSON，而不是二进制协议？

**1. 零工具依赖，人人可调试。**

不需要专用上位机。用任何串口终端（PuTTY、miniterm、screen、Arduino Serial Monitor）都能直接和设备交互：

```bash
echo '{"cmd":"can_start"}' > /dev/ttyACM0
echo '{"cmd":"send","id":0x123,"ext":false,"data":[1,2,3]}' > /dev/ttyACM0
```

二进制协议做不到这一点——你必须写一个专用的编解码器才能和设备对话。对于一个"替代封闭厂商模块"的工具来说，**开放的第一步就是让人能不写代码就上手**。

**2. 协议自描述，所见即所得。**

对比一下同一条 CAN 帧的两种格式：

```
JSON:   {"type":"rx","id":0x123,"ext":false,"dlc":3,"data":[1,2,3],"timestamp_ms":12345}
Binary: 02 00 00 01 23 00 03 01 02 03 00 00 30 39
```

JSON 版本你盯着看就能理解；二进制版本你需要翻协议文档对着字节偏移量数。调试 CAN 总线本身已经够复杂了，**通信协议不应该成为另一个需要调试的黑盒**。

**3. 向前兼容，无需协议版本号。**

加新字段不破坏旧解析器。`{"cmd":"send","id":123,"data":[1]}` 和 `{"cmd":"send","id":123,"data":[1],"dlc":8}` 在同一个固件上都能工作——旧固件忽略不认识的新字段，新固件利用新字段提供更精确的控制。二进制协议要做到这一点，通常需要 TLV（Type-Length-Value）编码或协议版本协商。

**4. 二进制协议在这个项目里不是正确答案。**

切换到二进制（MessagePack、CBOR、自定义帧格式）能降低约 70% 的每帧字节开销，但我们仔细算过账：

| | JSON @ 115200bps | Binary @ 115200bps | CAN 总线 @ 500kbps |
|---|---|---|---|
| 理论最大吞吐 | ~115 帧/秒 | ~820 帧/秒 | ~3000 帧/秒 |
| 日常调试场景 | ✅ 够用 | ✅ 够用 | — |

在 CAN 开发调试的日常场景中（单帧发送、周期信号监控、低速到中速总线），JSON 的吞吐量完全够用。而在真正的高负载 CAN 总线（80%+ 负载率）下，**二进制协议同样不够**——限制因子不是协议开销，而是 115200 波特率的 USB CDC 物理带宽。突破这个瓶颈需要提高波特率或切换到 USB Bulk 传输，与协议格式无关。

**5. 承认的权衡。**

- **带宽效率**：每帧约 100 字节，对比二进制约 14 字节。在意这个差距的场景（CAN 总线长时间数据记录、高吞吐离线分析），建议直接提高 USB CDC 波特率（921600 → 约 900 帧/秒 JSON），或在未来实现可选的二进制流模式。
- **解析开销**：ESP32 端需要解析 JSON 文本。当前的手写解析器约 300 行 C 代码，无堆分配，够用——但需要持续打磨边界情况。
- **CAN FD**：JSON 协议本身不限制 CAN FD（64 字节数据），但当前固件只支持 Classical CAN 2.0。

**总结**：这是一个**调试工具**，不是高性能汽车 CAN 数据记录仪。JSON 的选择反映了"先让人理解，再让机器优化"的优先级。如果你需要 3000+ 帧/秒的持续记录能力，考虑专业的 CAN 分析仪；如果你需要在开发和调试中快速、灵活地控制 CAN 总线，这个工具就是为你设计的。

---

## 固件开发

三个独立 ESP-IDF 项目，按顺序构建：

```bash
# 在 ESP-IDF 6.0 环境中执行

# 1. CAN 硬件测试
cd software/esp32/test/twai_loopback
idf.py set-target esp32s3 && idf.py build

# 2. USB 串口测试
cd software/esp32/test/usb_cdc_echo
idf.py set-target esp32s3 && idf.py build

# 3. 综合桥接程序
cd software/esp32/can_bridge
idf.py set-target esp32s3 && idf.py build
```

详见 [docs/firmware.md](docs/firmware.md) 和 [docs/getting_started.md](docs/getting_started.md)。

---

## PC 上位机

PySide6 + pyserial，线程安全串口通信，多标签页可扩展架构。

```bash
cd software/pc
uv sync
uv run python main.py
```

---

## License

MIT License
