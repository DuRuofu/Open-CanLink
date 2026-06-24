# CLAUDE.md

本项目是 **open-can-link CAN 总线调试工具** 的 PC 上位机，基于 PySide6 构建。

通过 USB CDC 虚拟串口与硬件设备（ESP32-S3 / CH32V203）通信，使用 JSON 换行分隔协议完成 CAN 2.0 帧的收发、过滤、周期发送、状态监控等功能。

同时它也是一个通用的**串口上位机模板**——复制到其他项目后，协议解析层（LineBuffer + JSON 编解码）无需修改，只需添加新的标签页来处理特定设备的 CAN 帧含义、绘制控制面板即可。

## 适用场景

- **当前项目**：CAN 总线调试，连接到 open-can-link 硬件（ESP32-S3 / CH32V203 + TJA1051T）
- **作为模板**：复制到新项目，替换协议和页面，适配任何通过串口 JSON 通信的设备

**复制模板 → 替换协议解析 → 添加标签页 → 完成。**

## 通信协议

本项目使用 **JSON 换行分隔协议**，完整定义见项目根目录 `docs/protocol.md`。

上位机通过串口连接硬件设备（ESP32-S3 方案 A 或 CH32V203 方案 B），两者共用同一套协议。

### 物理层

- 串口通信，波特率可配置（9600 ~ 921600）
- 数据以换行符 `\n` 分隔，每行一条完整的 JSON 消息
- 编码：UTF-8
- 支持 `\r\n` (Windows) 和 `\n` (Unix) 两种换行

### 消息格式

设备 → 上位机（推送数据）：

```json
{"type": "rx", "id": 291, "ext": false, "dlc": 4, "data": [1, 2, 3, 4], "timestamp_ms": 12345}
```

上位机 → 设备（发送命令）：

```json
{"cmd": "send", "id": 291, "ext": false, "data": [1, 2, 3, 4]}
```

设备收到命令后返回响应：

```json
{"type": "response", "cmd": "send", "status": "ok", "message": ""}
```

### 消息类型说明

| 字段 | 含义 |
|------|------|
| `type` | 消息类型：`"rx"` (接收帧)、`"status"` (状态)、`"response"` (命令响应)、`"info"` (设备信息) |
| `cmd` | 命令名 |
| `status` | `"ok"` 或 `"error"` |

### 协议解析类 (`core/can_protocol.py`)

`LineBuffer` 负责处理串口粘包/拆包：

```python
from core.can_protocol import LineBuffer, parse_message

buffer = LineBuffer()
for line in buffer.feed(raw_bytes):
    msg = parse_message(line)  # 返回 CanFrame / CanStatus / CommandResponse / DeviceInfo 或 None
```

项目已内置 CAN 协议的数据模型（`CanFrame`、`CanStatus`、`CommandResponse`、`DeviceInfo`），新项目可根据实际协议替换或扩展 `parse_message()` 和对应的 dataclass。

---

## 架构

```
software/pc/
├── main.py                     # 程序入口
├── CLAUDE.md                   # 本文件
├── logo.png                    # 应用图标
├── pyproject.toml              # 依赖声明
├── core/
│   ├── serial_manager.py       # 线程安全的串口管理器
│   ├── log_manager.py          # 彩色日志管理器
│   └── can_protocol.py         # 协议编解码 (LineBuffer + parse_message + build_*)
└── ui/
    ├── main_window.py          # 主窗口 QSplitter 左右布局
    ├── serial_panel.py         # 左侧：串口配置面板 (端口选择/波特率/连接)
    ├── log_panel.py            # 左侧：日志面板 (彩色日志/清除/保存)
    ├── tab_widget.py           # 右侧：标签页容器
    └── tabs/
        ├── can_tab.py          # CAN 收发 (监控表格 + 发送面板 + 周期发送 + 面板设置)
        ├── quick_cmd_tab.py    # 快捷指令 (一键发送保存的命令)
        ├── filter_tab.py       # 过滤器配置 (CAN 硬件过滤)
        └── data_exchange_tab.py # 通用数据收发 (文本收发，可用于调试自定义协议)
```

### 数据流

```
SerialManager (QThread 轮询串口)
    │  data_received 信号 (bytes)
    ▼
MainWindow._on_data_received(data)
    │  路由到所有标签页的 on_data_received(data)
    ▼
CanTab.on_data_received(data)
    │  LineBuffer.feed(data) → parse_message(line)
    ▼
    ├── CanFrame   → 更新监控表格
    ├── CanStatus  → 更新状态显示
    ├── CommandResponse → 日志记录
    └── DeviceInfo → 日志记录
```

### 发送流程

```
CanTab.send_requested 信号 (str)
    │
    ▼
MainWindow.send_data(data)
    │
    ▼
SerialManager.send(data)
    │  QMutex 保护
    ▼
串口发送 (bytes)
```

---

## 核心类

### SerialManager

线程安全的串口管理器。

```
连接/断开:
  connect(port: str, baudrate: str, log_manager: LogManager) → None
  disconnect() → None

发送:
  send(data: str | bytes) → None    # 线程安全，有 mutex 保护

信号:
  connected()                       # 连接成功
  disconnected()                    # 断开
  data_received(data: bytes)        # 收到数据
  error_occurred(msg: str)          # 错误

属性:
  is_connected: bool                # 是否已连接
```

内部实现：`SerialWorker` 在独立 `QThread` 中运行，用 10ms 定时器轮询 `serial.read_all()`，通过信号将数据发回主线程。串口打开失败或设备断开时自动发出 `error_occurred` 信号。

### LogManager

彩色 HTML 日志管理器。

```
日志方法:
  info(msg)     → 灰色
  send(msg)     → 蓝色 (发送方向)
  recv(msg)     → 绿色 (接收方向)
  error(msg)    → 红色
  warning(msg)  → 橙色

管理:
  set_text_browser(browser) → 绑定 QTextBrowser 显示组件
  clear()                   → 清空
  save_to_file(path)        → 保存为文本文件

上限: 5000 条日志块，超出自动裁剪
```

### LineBuffer (`core/can_protocol.py`)

处理串口数据粘包/拆包：

```python
buffer = LineBuffer()

# 每次收到串口数据时调用
lines = buffer.feed(raw_bytes)  # 返回完整行列表

# 连接断开时调用
buffer.reset()
```

### MainWindow

主窗口，QSplitter 左右布局（左 220px + 右 780px）。

连接时自动发送 `can_start` + `get_info` 命令。断开时自动保存设置（QSettings）。

---

## 添加新标签页

### 1. 创建标签页类

在 `ui/tabs/` 下新建文件，继承 `QWidget`：

```python
from PySide6.QtWidgets import QWidget
from PySide6.QtCore import Signal
from core.can_protocol import LineBuffer, parse_message

class MyTab(QWidget):
    send_requested = Signal(str)  # 向串口发送数据

    def __init__(self):
        super().__init__()
        self._buffer = LineBuffer()
        self._setup_ui()

    def _setup_ui(self):
        # 构建你的 UI
        pass

    def on_data_received(self, data: bytes):
        """接收串口数据。MainWindow 会在每次收到数据时调用。"""
        for line in self._buffer.feed(data):
            msg = parse_message(line)
            if msg:
                self._handle_message(msg)

    def _handle_message(self, msg):
        """根据消息类型更新 UI"""
        from core.can_protocol import CanFrame, CanStatus
        if isinstance(msg, CanFrame):
            # 更新帧显示
            pass
        elif isinstance(msg, CanStatus):
            # 更新状态
            pass

    def set_connected(self, connected: bool):
        """连接状态变化时调用。用于启用/禁用控件。"""
        pass
```

### 2. 注册到主窗口

在 `main_window.py` 的 `_setup_ui()` 中添加：

```python
from .tabs.my_tab import MyTab

# ...
self.my_tab = MyTab()
self.tab_widget.add_tab(self.my_tab, "我的面板")

# 如果有发送需求，连接信号:
self.my_tab.send_requested.connect(self.send_data)
```

在 `_connect_signals()` 中更新 `_on_serial_connected` / `_on_serial_disconnected` 的 tab 列表。

### 3. 注册 QSettings 持久化

如果标签页需要保存设置，使用 `QSettings("open-can-link", "SettingName")` 读写配置：

```python
from PySide6.QtCore import QSettings

s = QSettings("open-can-link", "MySettings")
s.setValue("key", value)     # 保存
value = s.value("key", default)  # 恢复
```

---

## 为新设备适配（应用层）

复制模板到新项目时，底层协议（LineBuffer、`parse_message`、`build_*`）不变——你始终通过 CAN 桥接硬件与设备通信。

工作只在**应用层**：添加标签页，把特定 CAN ID 的帧解释为设备数据（温度、速度、位置……），或把控制动作编码为 CAN 帧发送。

### 模式：添加设备专属标签页

```python
from PySide6.QtWidgets import QWidget, QLabel
from PySide6.QtCore import Signal
from core.can_protocol import LineBuffer, parse_message, CanFrame, build_send

class MotorControlTab(QWidget):
    """电机控制面板 — 解析特定 CAN ID，发送控制帧"""
    send_requested = Signal(str)

    def __init__(self):
        super().__init__()
        self._buffer = LineBuffer()
        self._setup_ui()

    def _setup_ui(self):
        self.label_speed = QLabel("转速: --")
        # ... 按钮、滑块、仪表等

    def on_data_received(self, data: bytes):
        for line in self._buffer.feed(data):
            msg = parse_message(line)
            if isinstance(msg, CanFrame):
                self._handle_can_frame(msg)

    def _handle_can_frame(self, frame: CanFrame):
        """根据 CAN ID 解释数据"""
        if frame.id == 0x201:           # 电机状态帧
            speed = frame.data[0] | (frame.data[1] << 8)  # 转速 RPM
            self.label_speed.setText(f"转速: {speed} RPM")
        elif frame.id == 0x301:         # 温度传感器
            temp = frame.data[0]
            self.label_temp.setText(f"温度: {temp} °C")

    def on_btn_set_speed(self):
        """发送电机调速命令"""
        speed = 1500  # 目标转速
        # 使用现有的 build_send，无需修改协议层
        cmd = build_send(can_id=0x200, ext=False, data=[speed & 0xFF, (speed >> 8) & 0xFF])
        self.send_requested.emit(cmd)
```

**核心思路**：协议层是稳定的，变化的是"哪个 CAN ID 代表什么"和"用什么 UI 展示/控制"。每个新设备 = 一套新标签页。

---

## 运行

```bash
cd software/pc
uv sync                    # 安装依赖
uv run python main.py      # 启动
```

依赖：`pyside6>=6.9.1`、`pyserial>=3.5`

打包（可选）：`pyproject.toml` 已配置 PyInstaller dev 依赖。

---

## 设计约定

- **信号驱动**：Tab 通过 `send_requested` 信号发送数据，不直接调用 SerialManager
- **主线程安全**：串口 I/O 在 QThread 中，UI 更新通过信号自动回到主线程
- **QSettings 键**：统一使用 `"open-can-link"` 组织名
- **日志风格**：发送数据用 `log_manager.send()`，接收用 `log_manager.recv()`
- **Tab 接口约定**：每个 Tab 应实现 `on_data_received(data)` 和 `set_connected(bool)`
