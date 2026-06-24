"""Main Window - 主窗口，左右分割布局"""
import time
from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QVBoxLayout, QSplitter, QSizePolicy, QFileDialog
)
from PySide6.QtCore import Qt

from .serial_panel import SerialPanel
from .log_panel import LogPanel
from .tab_widget import TabWidget
from .tabs.can_tab import CanTab
from .tabs.quick_cmd_tab import QuickCmdTab
from .tabs.filter_tab import FilterTab
from core.can_protocol import build_can_start, build_can_stop, build_get_info


class MainWindow(QWidget):
    """主窗口"""

    def __init__(self, serial_manager, log_manager):
        super().__init__()
        self._serial_manager = serial_manager
        self._log_manager = log_manager
        self._setup_ui()
        self._connect_signals()

    def closeEvent(self, event):
        if self._serial_manager.is_connected:
            self._serial_manager.disconnect()
        time.sleep(0.05)
        event.accept()

    def _setup_ui(self):
        self.setWindowTitle("CAN 调试助手")
        self.setGeometry(100, 100, 1200, 800)

        main_layout = QHBoxLayout(self)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        main_layout.addWidget(splitter)

        # 左侧面板
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 0, 0)

        self.serial_panel = SerialPanel()
        self.serial_panel.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        left_layout.addWidget(self.serial_panel)

        self.log_panel = LogPanel()
        left_layout.addWidget(self.log_panel, stretch=1)

        # 右侧: 三个功能页面
        self.tab_widget = TabWidget()
        self.can_tab = CanTab()
        self.tab_widget.add_tab(self.can_tab, "CAN 收发")
        self.quick_cmd_tab = QuickCmdTab()
        self.tab_widget.add_tab(self.quick_cmd_tab, "快捷指令")
        self.filter_tab = FilterTab()
        self.tab_widget.add_tab(self.filter_tab, "过滤器")

        splitter.addWidget(left_widget)
        splitter.addWidget(self.tab_widget)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 4)
        splitter.setSizes([220, 780])

    def _connect_signals(self):
        self._log_manager.set_text_browser(self.log_panel.get_log_browser())

        self.serial_panel.btn_refresh.clicked.connect(self._on_refresh_ports)
        self.serial_panel.btn_connect.clicked.connect(self._on_toggle_connection)

        self._serial_manager.connected.connect(self._on_serial_connected)
        self._serial_manager.disconnected.connect(self._on_serial_disconnected)
        self._serial_manager.error_occurred.connect(self._on_serial_error)
        self._serial_manager.data_received.connect(self._on_data_received)

        self.log_panel.btn_clear.clicked.connect(self._log_manager.clear)
        self.log_panel.btn_save.clicked.connect(self._on_save_log)

        self.can_tab.send_requested.connect(self.send_data)
        self.can_tab.add_to_quick_cmd.connect(self.quick_cmd_tab.add_from_can_tab)
        self.quick_cmd_tab.send_requested.connect(self.send_data)
        self.filter_tab.send_requested.connect(self.send_data)

    def _on_refresh_ports(self):
        import serial.tools.list_ports
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.serial_panel.set_port_list(ports)

    def _on_toggle_connection(self):
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
        for tab in [self.can_tab, self.quick_cmd_tab, self.filter_tab]:
            tab.set_connected(True)
        self._log_manager.info("串口已连接")
        self._serial_manager.send(build_can_start())
        self._serial_manager.send(build_get_info())

    def _on_serial_disconnected(self):
        self.serial_panel.set_connected_state(False)
        for tab in [self.can_tab, self.quick_cmd_tab, self.filter_tab]:
            tab.set_connected(False)
        self._log_manager.info("串口已断开")

    def _on_serial_error(self, msg):
        self._log_manager.error(msg)

    def _on_data_received(self, data):
        for i in range(self.tab_widget.count()):
            widget = self.tab_widget.widget(i)
            if hasattr(widget, 'on_data_received'):
                widget.on_data_received(data)

    def _on_save_log(self):
        path, _ = QFileDialog.getSaveFileName(self, "保存日志", "", "Text Files (*.txt)")
        if path:
            self._log_manager.save_to_file(path)

    def send_data(self, data):
        return self._serial_manager.send(data)

    def get_log_manager(self):
        return self._log_manager
