"""Main Window - 主窗口，左右分割布局"""
from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QVBoxLayout, QSplitter, QSizePolicy
)
from PySide6.QtCore import Qt

from .serial_panel import SerialPanel
from .log_panel import LogPanel
from .tab_widget import TabWidget
from .tabs.data_exchange_tab import DataExchangeTab
from .tabs.can_monitor_tab import CanMonitorTab
from .tabs.can_send_tab import CanSendTab


class MainWindow(QWidget):
    """主窗口 - 左侧串口配置+日志，右侧多标签页"""

    def __init__(self, serial_manager, log_manager):
        super().__init__()
        self._serial_manager = serial_manager
        self._log_manager = log_manager
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        self.setWindowTitle("串口调试工具")
        self.setGeometry(100, 100, 1200, 800)

        main_layout = QHBoxLayout(self)

        # 创建分割器
        splitter = QSplitter(Qt.Orientation.Horizontal)
        main_layout.addWidget(splitter)

        # 左侧面板（垂直布局：串口配置 + 日志）
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 0, 0)

        # 串口配置（固定高度，不扩展）
        self.serial_panel = SerialPanel()
        self.serial_panel.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        left_layout.addWidget(self.serial_panel)

        # 日志面板（占据剩余空间）
        self.log_panel = LogPanel()
        left_layout.addWidget(self.log_panel, stretch=1)

        # 右侧标签页
        self.tab_widget = TabWidget()
        self.tab_widget.add_tab(DataExchangeTab(), "数据收发")
        self.can_monitor_tab = CanMonitorTab()
        self.tab_widget.add_tab(self.can_monitor_tab, "CAN 监视器")
        self.can_send_tab = CanSendTab()
        self.tab_widget.add_tab(self.can_send_tab, "CAN 发送")

        # 添加到分割器
        splitter.addWidget(left_widget)
        splitter.addWidget(self.tab_widget)

        # 设置左右比例 1:4
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 4)
        splitter.setSizes([250, 750])

        # 设置尺寸策略
        left_widget.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Expanding)
        self.tab_widget.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Expanding)

    def _connect_signals(self):
        # 关联日志管理器
        self._log_manager.set_text_browser(self.log_panel.get_log_browser())

        # 串口配置信号
        self.serial_panel.btn_refresh.clicked.connect(self._on_refresh_ports)
        self.serial_panel.btn_connect.clicked.connect(self._on_toggle_connection)

        # 连接状态变化
        self._serial_manager.connected.connect(self._on_serial_connected)
        self._serial_manager.disconnected.connect(self._on_serial_disconnected)
        self._serial_manager.error_occurred.connect(self._on_serial_error)
        self._serial_manager.data_received.connect(self._on_data_received)

        # 日志按钮
        self.log_panel.btn_clear.clicked.connect(self._log_manager.clear)
        self.log_panel.btn_save.clicked.connect(self._on_save_log)

        # CAN 发送信号
        self.can_send_tab.send_requested.connect(self.send_data)

    def _on_refresh_ports(self):
        """刷新串口列表"""
        import serial.tools.list_ports
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.serial_panel.set_port_list(ports)

    def _on_toggle_connection(self):
        """切换连接状态"""
        if self._serial_manager.is_connected:
            self._serial_manager.disconnect()
        else:
            port = self.serial_panel.get_port()
            baudrate = self.serial_panel.get_baudrate()
            if not port:
                self._log_manager.warning("请先选择串口")
                return
            self._serial_manager.connect(port, baudrate, self._log_manager)

    def _on_serial_connected(self):
        self.serial_panel.set_connected_state(True)
        self._log_manager.info("串口已连接")

    def _on_serial_disconnected(self):
        self.serial_panel.set_connected_state(False)
        self._log_manager.info("串口已断开")

    def _on_serial_error(self, msg):
        self._log_manager.error(msg)

    def _on_data_received(self, data):
        # 广播给所有 tab（CAN 监视器需要持续接收数据）
        for i in range(self.tab_widget.count()):
            widget = self.tab_widget.widget(i)
            if hasattr(widget, 'on_data_received'):
                widget.on_data_received(data)

    def _on_save_log(self):
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getSaveFileName(self, "保存日志", "", "Text Files (*.txt)")
        if path:
            self._log_manager.save_to_file(path)

    def send_data(self, data):
        """发送数据"""
        return self._serial_manager.send(data)

    def get_log_manager(self):
        return self._log_manager