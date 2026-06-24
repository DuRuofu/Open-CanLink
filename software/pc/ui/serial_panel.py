"""Serial Panel - 串口配置面板"""
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QComboBox, QPushButton, QSizePolicy
)
from PySide6.QtCore import Qt, QSettings


class SerialPanel(QWidget):
    """串口配置面板 - 串口选择、波特率、连接按钮"""

    def __init__(self):
        super().__init__()
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        group = QGroupBox("串口配置")
        grid = QGridLayout(group)

        # 串口选择
        grid.addWidget(QLabel("串口:"), 0, 0)
        self.combo_ports = QComboBox()
        self.combo_ports.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        grid.addWidget(self.combo_ports, 0, 1)

        # 刷新按钮
        self.btn_refresh = QPushButton("刷新")
        self.btn_refresh.setFixedWidth(60)
        grid.addWidget(self.btn_refresh, 0, 2)

        # 波特率选择
        grid.addWidget(QLabel("波特率:"), 1, 0)
        self.combo_baudrate = QComboBox()
        self.combo_baudrate.addItems(["9600", "19200", "38400", "57600", "115200",
                                       "230400", "460800", "921600"])
        self.combo_baudrate.setCurrentText("115200")
        self.combo_baudrate.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        grid.addWidget(self.combo_baudrate, 1, 1, 1, 2)

        # 连接按钮
        self.btn_connect = QPushButton("连接")
        grid.addWidget(self.btn_connect, 2, 0, 1, 3)

        layout.addWidget(group)
        layout.addStretch()

    def get_port(self):
        return self.combo_ports.currentText()

    def get_baudrate(self):
        return self.combo_baudrate.currentText()

    def set_port_list(self, ports):
        """设置串口列表"""
        self.combo_ports.clear()
        self.combo_ports.addItems(ports)

    def set_connected_state(self, connected):
        """设置连接后的界面状态"""
        self.combo_ports.setEnabled(not connected)
        self.combo_baudrate.setEnabled(not connected)
        self.btn_refresh.setEnabled(not connected)
        self.btn_connect.setText("断开" if connected else "连接")

    def save_settings(self):
        """持久化当前配置到 QSettings"""
        s = QSettings("esp-can-link", "SerialPort")
        s.setValue("port", self.combo_ports.currentText())
        s.setValue("baudrate", self.combo_baudrate.currentText())

    def restore_settings(self):
        """从 QSettings 恢复上次的配置"""
        s = QSettings("esp-can-link", "SerialPort")
        last_port = s.value("port", "")
        last_baud = s.value("baudrate", "115200")
        if last_port:
            idx = self.combo_ports.findText(last_port)
            if idx >= 0:
                self.combo_ports.setCurrentIndex(idx)
        idx = self.combo_baudrate.findText(last_baud)
        if idx >= 0:
            self.combo_baudrate.setCurrentIndex(idx)