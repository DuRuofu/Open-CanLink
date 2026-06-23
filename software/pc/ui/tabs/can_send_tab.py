"""
CAN Send Tab - Construct and send CAN frames via JSON protocol.
"""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QLineEdit, QPushButton, QComboBox, QTextBrowser,
    QCheckBox, QSpinBox,
)
from PySide6.QtCore import Qt, Signal

from core.can_protocol import (
    build_send, build_periodic_start, build_periodic_stop,
    build_set_bitrate, build_can_start, build_can_stop,
    build_get_status, build_get_info,
)


class CanSendTab(QWidget):
    """Tab for constructing and sending CAN commands."""

    # Signal: request to send raw data via serial
    send_requested = Signal(str)

    # Signal: response received
    response_received_signal = Signal(object)

    BITRATES = ["125000", "250000", "500000", "1000000"]

    def __init__(self):
        super().__init__()
        self._is_periodic_active = False
        self._periodic_id = 0
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # ---- CAN Control Group ----
        ctrl_group = QGroupBox("CAN 控制")
        ctrl_layout = QHBoxLayout(ctrl_group)

        ctrl_layout.addWidget(QLabel("波特率:"))
        self.combo_bitrate = QComboBox()
        self.combo_bitrate.addItems(self.BITRATES)
        self.combo_bitrate.setCurrentText("500000")
        ctrl_layout.addWidget(self.combo_bitrate)

        self.btn_set_bitrate = QPushButton("设置波特率")
        self.btn_set_bitrate.clicked.connect(self._on_set_bitrate)
        ctrl_layout.addWidget(self.btn_set_bitrate)

        self.btn_can_start = QPushButton("CAN Start")
        ctrl_layout.addWidget(self.btn_can_start)
        self.btn_can_start.clicked.connect(lambda: self._send_json(build_can_start()))
        ctrl_layout.addWidget(self.btn_can_start)

        self.btn_can_stop = QPushButton("CAN Stop")
        self.btn_can_stop.clicked.connect(lambda: self._send_json(build_can_stop()))
        ctrl_layout.addWidget(self.btn_can_stop)

        ctrl_layout.addStretch()

        self.btn_get_status = QPushButton("状态")
        self.btn_get_status.clicked.connect(lambda: self._send_json(build_get_status()))
        ctrl_layout.addWidget(self.btn_get_status)

        self.btn_get_info = QPushButton("设备信息")
        self.btn_get_info.clicked.connect(lambda: self._send_json(build_get_info()))
        ctrl_layout.addWidget(self.btn_get_info)

        layout.addWidget(ctrl_group)

        # ---- CAN Frame Group ----
        frame_group = QGroupBox("CAN 帧发送")
        frame_layout = QGridLayout(frame_group)

        # ID
        frame_layout.addWidget(QLabel("CAN ID:"), 0, 0)
        self.edit_id = QLineEdit("0x123")
        self.edit_id.setMaximumWidth(120)
        frame_layout.addWidget(self.edit_id, 0, 1)

        self.chk_ext = QCheckBox("扩展帧 (29-bit)")
        frame_layout.addWidget(self.chk_ext, 0, 2)

        # DLC
        frame_layout.addWidget(QLabel("DLC:"), 1, 0)
        self.spin_dlc = QSpinBox()
        self.spin_dlc.setRange(0, 8)
        self.spin_dlc.setValue(4)
        self.spin_dlc.valueChanged.connect(self._on_dlc_changed)
        self.spin_dlc.setMaximumWidth(60)
        frame_layout.addWidget(self.spin_dlc, 1, 1)

        # Data bytes
        frame_layout.addWidget(QLabel("数据 (Hex):"), 2, 0)
        self.data_edits: list[QLineEdit] = []
        data_row = QHBoxLayout()
        for i in range(8):
            edit = QLineEdit("00")
            edit.setMaximumWidth(35)
            edit.setMaxLength(2)
            edit.setAlignment(Qt.AlignmentFlag.AlignCenter)
            edit.setFont(self.font())
            self.data_edits.append(edit)
            data_row.addWidget(edit)
            if i == 3:
                sep = QLabel(" ")
                data_row.addWidget(sep)
        data_row.addStretch()
        frame_layout.addLayout(data_row, 2, 1, 1, 2)

        self._on_dlc_changed(4)

        # Buttons
        btn_row = QHBoxLayout()

        self.btn_send = QPushButton("发送一次")
        self.btn_send.clicked.connect(self._on_send_once)
        btn_row.addWidget(self.btn_send)

        btn_row.addWidget(QLabel("周期(ms):"))
        self.edit_period = QLineEdit("100")
        self.edit_period.setMaximumWidth(60)
        btn_row.addWidget(self.edit_period)

        self.btn_periodic = QPushButton("开始周期发送")
        self.btn_periodic.clicked.connect(self._on_toggle_periodic)
        btn_row.addWidget(self.btn_periodic)

        btn_row.addStretch()
        frame_layout.addLayout(btn_row, 3, 0, 1, 3)

        layout.addWidget(frame_group)

        # ---- Sent History ----
        hist_group = QGroupBox("发送记录")
        hist_layout = QVBoxLayout(hist_group)
        self.history_browser = QTextBrowser()
        self.history_browser.setMaximumHeight(150)
        self.history_browser.setFont(self.font())
        hist_layout.addWidget(self.history_browser)
        layout.addWidget(hist_group)

    def _on_send_once(self):
        """Build and emit a single send command."""
        try:
            can_id, ext = self._get_id()
            dlc = self.spin_dlc.value()
            data = self._get_data(dlc)
        except ValueError as e:
            self._log(f"错误: {e}")
            return

        json_str = build_send(can_id, ext, data)
        self._send_json(json_str)

    def _on_toggle_periodic(self):
        """Start or stop periodic sending."""
        if self._is_periodic_active:
            json_str = build_periodic_stop(self._periodic_id)
            self._send_json(json_str)
            self._is_periodic_active = False
            self.btn_periodic.setText("开始周期发送")
            self._log(f"停止周期发送 ID=0x{self._periodic_id:X}")
        else:
            try:
                can_id, ext = self._get_id()
                dlc = self.spin_dlc.value()
                data = self._get_data(dlc)
                period_ms = int(self.edit_period.text())
                if period_ms < 1:
                    raise ValueError("周期必须 >= 1 ms")
            except ValueError as e:
                self._log(f"错误: {e}")
                return

            self._periodic_id = can_id
            json_str = build_periodic_start(can_id, ext, data, period_ms)
            self._send_json(json_str)
            self._is_periodic_active = True
            self.btn_periodic.setText("停止周期发送")
            self._log(f"开始周期发送 ID=0x{can_id:X} 周期={period_ms}ms")

    def _on_set_bitrate(self):
        bitrate = int(self.combo_bitrate.currentText())
        self._send_json(build_set_bitrate(bitrate))

    def _send_json(self, json_str: str):
        """Emit send_requested and log."""
        self.send_requested.emit(json_str)
        self._log(f">>> {json_str.strip()}")

    def _log(self, msg: str):
        self.history_browser.append(msg)

    def _get_id(self) -> tuple[int, bool]:
        """Parse CAN ID from input. Returns (id, is_ext)."""
        text = self.edit_id.text().strip()
        if text.startswith("0x") or text.startswith("0X"):
            can_id = int(text, 16)
        else:
            can_id = int(text)

        ext = self.chk_ext.isChecked()
        max_id = 0x1FFFFFFF if ext else 0x7FF
        if can_id < 0 or can_id > max_id:
            raise ValueError(f"ID 超出范围 (max {max_id})")
        return can_id, ext

    def _get_data(self, dlc: int) -> list[int]:
        """Parse data bytes from hex inputs. Returns list of ints."""
        data = []
        for i in range(dlc):
            text = self.data_edits[i].text().strip()
            if not text:
                text = "00"
            val = int(text, 16)
            if val < 0 or val > 255:
                raise ValueError(f"Byte {i} 超出范围")
            data.append(val)
        return data

    def _on_dlc_changed(self, dlc: int):
        """Enable/disable data byte inputs based on DLC."""
        for i, edit in enumerate(self.data_edits):
            edit.setEnabled(i < dlc)
            if i >= dlc:
                edit.setText("00")

    def on_data_received(self, data: str):
        """Handle serial data (for logging responses)."""
        # Could parse responses here, but main_window already logs them
        pass

    def append_response(self, text: str):
        """Append response text to history."""
        self._log(f"<<< {text.strip()}")
