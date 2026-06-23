"""
CAN Send Tab - Construct and send CAN frames via JSON protocol.
"""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QLineEdit, QPushButton, QComboBox, QTextBrowser,
    QSpinBox, QRadioButton, QButtonGroup, QTableWidget, QTableWidgetItem,
    QHeaderView, QAbstractItemView, QMessageBox, QApplication,
)
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QRegularExpressionValidator, QColor

from core.can_protocol import (
    build_send, build_periodic_start, build_periodic_stop,
    build_set_bitrate,
    build_get_status, build_get_info,
    LineBuffer, parse_message, CanStatus, CommandResponse, DeviceInfo,
)

MAX_PERIODIC_SLOTS = 4


class CanSendTab(QWidget):
    """Tab for constructing and sending CAN commands."""

    send_requested = Signal(str)

    BITRATES = ["125K", "250K", "500K", "1M"]
    BITRATE_VALUES = {"125K": 125000, "250K": 250000, "500K": 500000, "1M": 1000000}

    def __init__(self):
        super().__init__()
        self._active_periodic_ids: set[int] = set()
        self._line_buffer = LineBuffer()
        self._is_connected = False
        self._is_can_running = False
        self._setup_ui()

    # ── UI Setup ─────────────────────────────────────────────

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # ── CAN Control ──
        ctrl_group = QGroupBox("CAN 控制")
        ctrl_layout = QHBoxLayout(ctrl_group)

        ctrl_layout.addWidget(QLabel("波特率:"))
        self.combo_bitrate = QComboBox()
        self.combo_bitrate.addItems(self.BITRATES)
        self.combo_bitrate.setCurrentText("500K")
        ctrl_layout.addWidget(self.combo_bitrate)

        self.btn_set_bitrate = QPushButton("设置波特率")
        self.btn_set_bitrate.clicked.connect(self._on_set_bitrate)
        ctrl_layout.addWidget(self.btn_set_bitrate)

        ctrl_layout.addStretch()

        self.btn_get_status = QPushButton("状态")
        self.btn_get_status.clicked.connect(lambda: self._send_json(build_get_status()))
        ctrl_layout.addWidget(self.btn_get_status)

        self.btn_get_info = QPushButton("设备信息")
        self.btn_get_info.clicked.connect(lambda: self._send_json(build_get_info()))
        ctrl_layout.addWidget(self.btn_get_info)

        self.lbl_can_status = QLabel("CAN: --")
        ctrl_layout.addWidget(self.lbl_can_status)

        layout.addWidget(ctrl_group)

        # ── CAN Frame Send ──
        frame_group = QGroupBox("CAN 帧发送")
        frame_layout = QGridLayout(frame_group)

        # ID + format radio
        frame_layout.addWidget(QLabel("CAN ID:"), 0, 0)
        self.edit_id = QLineEdit("0x123")
        self.edit_id.setMaximumWidth(120)
        frame_layout.addWidget(self.edit_id, 0, 1)

        self.id_hex_radio = QRadioButton("Hex")
        self.id_hex_radio.setChecked(True)
        self.id_dec_radio = QRadioButton("Dec")
        id_format_group = QButtonGroup(self)
        id_format_group.addButton(self.id_hex_radio)
        id_format_group.addButton(self.id_dec_radio)
        id_format_group.buttonClicked.connect(self._on_id_format_changed)

        id_fmt_row = QHBoxLayout()
        id_fmt_row.addWidget(self.id_hex_radio)
        id_fmt_row.addWidget(self.id_dec_radio)
        id_fmt_row.addStretch()
        frame_layout.addLayout(id_fmt_row, 0, 2)

        # Frame type radio
        frame_layout.addWidget(QLabel("帧类型:"), 1, 0)
        self.radio_std = QRadioButton("标准帧 (11-bit)")
        self.radio_std.setChecked(True)
        self.radio_ext = QRadioButton("扩展帧 (29-bit)")
        type_group = QButtonGroup(self)
        type_group.addButton(self.radio_std)
        type_group.addButton(self.radio_ext)

        type_row = QHBoxLayout()
        type_row.addWidget(self.radio_std)
        type_row.addWidget(self.radio_ext)
        type_row.addStretch()
        frame_layout.addLayout(type_row, 1, 1, 1, 2)

        # DLC
        frame_layout.addWidget(QLabel("DLC:"), 2, 0)
        self.spin_dlc = QSpinBox()
        self.spin_dlc.setRange(0, 8)
        self.spin_dlc.setValue(4)
        self.spin_dlc.valueChanged.connect(self._on_dlc_changed)
        self.spin_dlc.setMaximumWidth(60)
        frame_layout.addWidget(self.spin_dlc, 2, 1)

        # Data bytes
        frame_layout.addWidget(QLabel("Data:"), 3, 0)
        self.data_edits: list[QLineEdit] = []
        hex_validator = QRegularExpressionValidator("[0-9A-Fa-f]{0,2}")
        data_row = QHBoxLayout()
        for i in range(8):
            edit = QLineEdit("00")
            edit.setMaximumWidth(35)
            edit.setMaxLength(2)
            edit.setAlignment(Qt.AlignmentFlag.AlignCenter)
            edit.setValidator(hex_validator)
            edit.setFont(self.font())
            self.data_edits.append(edit)
            data_row.addWidget(edit)
            if i == 3:
                data_row.addWidget(QLabel(" "))
        data_row.addStretch()
        frame_layout.addLayout(data_row, 3, 1, 1, 2)

        self._on_dlc_changed(4)

        # Buttons
        btn_row = QHBoxLayout()

        self.btn_send = QPushButton("发送一次")
        self.btn_send.clicked.connect(self._on_send_once)
        btn_row.addWidget(self.btn_send)

        btn_row.addWidget(QLabel("  周期(ms):"))
        self.edit_period = QLineEdit("100")
        self.edit_period.setMaximumWidth(55)
        btn_row.addWidget(self.edit_period)

        self.btn_add_to_list = QPushButton("添加到发送列表")
        self.btn_add_to_list.clicked.connect(self._on_add_to_list)
        btn_row.addWidget(self.btn_add_to_list)

        btn_row.addStretch()
        frame_layout.addLayout(btn_row, 4, 0, 1, 3)

        layout.addWidget(frame_group)

        # ── Periodic Send List ──
        list_group = QGroupBox("周期发送列表")
        list_layout = QVBoxLayout(list_group)

        self.periodic_table = QTableWidget(0, 6)
        self.periodic_table.setHorizontalHeaderLabels(
            ["CAN ID", "类型", "数据", "周期(ms)", "状态", "操作"]
        )
        self.periodic_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        self.periodic_table.horizontalHeader().setStretchLastSection(True)
        self.periodic_table.setColumnWidth(0, 80)
        self.periodic_table.setColumnWidth(1, 40)
        self.periodic_table.setColumnWidth(2, 160)
        self.periodic_table.setColumnWidth(3, 70)
        self.periodic_table.setColumnWidth(4, 70)
        self.periodic_table.setColumnWidth(5, 120)
        self.periodic_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.periodic_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.periodic_table.verticalHeader().setVisible(False)
        self.periodic_table.setMaximumHeight(150)
        list_layout.addWidget(self.periodic_table)

        list_bar = QHBoxLayout()
        self.btn_stop_all = QPushButton("全部停止")
        self.btn_stop_all.clicked.connect(self._on_stop_all)
        list_bar.addWidget(self.btn_stop_all)
        self.btn_start_all = QPushButton("全部启动")
        self.btn_start_all.clicked.connect(self._on_start_all)
        list_bar.addWidget(self.btn_start_all)
        self.btn_remove_selected = QPushButton("移除选中")
        self.btn_remove_selected.clicked.connect(self._on_remove_selected)
        list_bar.addWidget(self.btn_remove_selected)
        list_bar.addStretch()
        self.lbl_slot_usage = QLabel(f"0/{MAX_PERIODIC_SLOTS}")
        list_bar.addWidget(self.lbl_slot_usage)
        list_layout.addLayout(list_bar)

        layout.addWidget(list_group)

        # ── Sent History ──
        hist_group = QGroupBox("发送记录")
        hist_layout = QVBoxLayout(hist_group)
        self.history_browser = QTextBrowser()
        self.history_browser.setMaximumHeight(150)
        self.history_browser.setFont(self.font())
        hist_layout.addWidget(self.history_browser)
        layout.addWidget(hist_group)

    # ── Connection state ─────────────────────────────────────

    def set_connected(self, connected: bool):
        """Enable/disable buttons based on connection state."""
        self._is_connected = connected
        self.btn_send.setEnabled(connected)
        self.btn_add_to_list.setEnabled(connected)
        self.btn_set_bitrate.setEnabled(connected)
        self.btn_get_status.setEnabled(connected)
        self.btn_get_info.setEnabled(connected)
        self.btn_stop_all.setEnabled(connected)
        self.btn_start_all.setEnabled(connected)
        self.btn_remove_selected.setEnabled(connected)
        if not connected:
            self._active_periodic_ids.clear()
            for row in range(self.periodic_table.rowCount()):
                w = self.periodic_table.cellWidget(row, 5)
                if w and isinstance(w, QPushButton):
                    w.setText("启动")
            self.lbl_can_status.setText("CAN: --")
            self._update_table_statuses()

    # ── Send once ────────────────────────────────────────────

    def _on_send_once(self):
        try:
            can_id, ext = self._get_id()
            dlc = self.spin_dlc.value()
            data = self._get_data(dlc)
            period_ms = int(self.edit_period.text() or "100")
            if period_ms < 1:
                raise ValueError("周期必须 >= 1 ms")
        except ValueError as e:
            self._log(f"错误: {e}")
            return

        self._send_json(build_send(can_id, ext, data))

    # ── Periodic list management ─────────────────────────────

    def _on_add_to_list(self):
        try:
            can_id, ext = self._get_id()
            dlc = self.spin_dlc.value()
            data = self._get_data(dlc)
            period_ms = int(self.edit_period.text() or "100")
            if period_ms < 1:
                raise ValueError("周期必须 >= 1 ms")
        except ValueError as e:
            self._log(f"错误: {e}")
            return

        # Check capacity
        current_count = self.periodic_table.rowCount()
        if current_count >= MAX_PERIODIC_SLOTS:
            QMessageBox.warning(self, "槽位已满",
                                f"最多支持 {MAX_PERIODIC_SLOTS} 个周期帧")
            return

        # Check duplicate ID
        for row in range(self.periodic_table.rowCount()):
            existing_id = self.periodic_table.item(row, 0)
            if existing_id and self._parse_table_id(existing_id.text()) == can_id:
                reply = QMessageBox.question(
                    self, "重复 ID",
                    f"CAN ID 0x{can_id:X} 已存在，是否覆盖？",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
                )
                if reply != QMessageBox.StandardButton.Yes:
                    return
                # Remove old row
                old_id = self._parse_table_id(existing_id.text())
                self.periodic_table.removeRow(row)
                self._active_periodic_ids.discard(old_id)
                break

        self._add_periodic_row(can_id, ext, dlc, data, period_ms, active=True)

    def _add_periodic_row(self, can_id: int, ext: bool, dlc: int,
                          data: list[int], period_ms: int, active: bool):
        row = self.periodic_table.rowCount()
        self.periodic_table.insertRow(row)

        id_str = f"0x{can_id:X}"
        type_str = "EXT" if ext else "STD"
        data_str = " ".join(f"{b:02X}" for b in data[:dlc]) if dlc > 0 else "--"

        self._set_cell(row, 0, id_str)
        self._set_cell(row, 1, type_str, Qt.AlignmentFlag.AlignCenter)
        self._set_cell(row, 2, data_str)
        self._set_cell(row, 3, str(period_ms), Qt.AlignmentFlag.AlignCenter)

        status_label = QLabel("运行中" if active else "已停止")
        status_label.setStyleSheet(
            f"color: {'green' if active else 'gray'}; font-weight: bold;"
        )
        self.periodic_table.setCellWidget(row, 4, status_label)

        btn = QPushButton("停止" if active else "启动")
        btn.clicked.connect(lambda: self._on_toggle_row_periodic(row))
        self.periodic_table.setCellWidget(row, 5, btn)

        # Store metadata in the table item data
        id_item = self.periodic_table.item(row, 0)
        if id_item:
            id_item.setData(Qt.ItemDataRole.UserRole, {
                "can_id": can_id, "ext": ext, "dlc": dlc,
                "data": data, "period_ms": period_ms,
            })

        if active:
            self._active_periodic_ids.add(can_id)
            self._send_json(build_periodic_start(can_id, ext, data[:dlc], period_ms))

        self._update_slot_usage()

    def _on_toggle_row_periodic(self, row: int):
        item = self.periodic_table.item(row, 0)
        if not item: return
        meta = item.data(Qt.ItemDataRole.UserRole)
        if not meta: return
        can_id = meta["can_id"]

        if can_id in self._active_periodic_ids:
            # Stop
            self._send_json(build_periodic_stop(can_id))
            self._active_periodic_ids.discard(can_id)
        else:
            # Start
            self._send_json(build_periodic_start(
                can_id, meta["ext"], meta["data"][:meta["dlc"]], meta["period_ms"]
            ))
            self._active_periodic_ids.add(can_id)

        self._update_row_status(row, can_id, can_id in self._active_periodic_ids)

    def _update_row_status(self, row: int, can_id: int, active: bool):
        w = self.periodic_table.cellWidget(row, 4)
        btn = self.periodic_table.cellWidget(row, 5)
        if isinstance(w, QLabel):
            w.setText("运行中" if active else "已停止")
            w.setStyleSheet(f"color: {'green' if active else 'gray'}; font-weight: bold;")
        if isinstance(btn, QPushButton):
            btn.setText("停止" if active else "启动")

    def _update_table_statuses(self):
        for row in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(row, 0)
            if not item: continue
            meta = item.data(Qt.ItemDataRole.UserRole)
            if not meta: continue
            can_id = meta["can_id"]
            self._update_row_status(row, can_id, can_id in self._active_periodic_ids)

    def _on_stop_all(self):
        for row in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(row, 0)
            if not item: continue
            meta = item.data(Qt.ItemDataRole.UserRole)
            if not meta: continue
            can_id = meta["can_id"]
            if can_id in self._active_periodic_ids:
                self._send_json(build_periodic_stop(can_id))
                self._active_periodic_ids.discard(can_id)
                self._update_row_status(row, can_id, False)

    def _on_start_all(self):
        for row in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(row, 0)
            if not item: continue
            meta = item.data(Qt.ItemDataRole.UserRole)
            if not meta: continue
            can_id = meta["can_id"]
            if can_id not in self._active_periodic_ids:
                self._send_json(build_periodic_start(
                    can_id, meta["ext"], meta["data"][:meta["dlc"]], meta["period_ms"]
                ))
                self._active_periodic_ids.add(can_id)
                self._update_row_status(row, can_id, True)

    def _on_remove_selected(self):
        rows = set()
        for idx in self.periodic_table.selectedIndexes():
            rows.add(idx.row())
        for row in sorted(rows, reverse=True):
            item = self.periodic_table.item(row, 0)
            if item:
                meta = item.data(Qt.ItemDataRole.UserRole)
                if meta:
                    can_id = meta["can_id"]
                    if can_id in self._active_periodic_ids:
                        self._send_json(build_periodic_stop(can_id))
                        self._active_periodic_ids.discard(can_id)
            self.periodic_table.removeRow(row)
        self._update_slot_usage()

    def _update_slot_usage(self):
        count = self.periodic_table.rowCount()
        self.lbl_slot_usage.setText(f"{count}/{MAX_PERIODIC_SLOTS}")

    def _parse_table_id(self, text: str) -> int:
        if text.startswith("0x") or text.startswith("0X"):
            return int(text, 16)
        return int(text)

    # ── ID parsing ───────────────────────────────────────────

    def _get_id(self) -> tuple[int, bool]:
        text = self.edit_id.text().strip()
        if self.id_hex_radio.isChecked():
            if text.lower().startswith("0x"):
                can_id = int(text, 16)
            else:
                can_id = int(text, 16) if all(c in "0123456789ABCDEFabcdef" for c in text) else int(text)
        else:
            can_id = int(text)

        ext = self.radio_ext.isChecked()
        max_id = 0x1FFFFFFF if ext else 0x7FF
        if can_id < 0 or can_id > max_id:
            raise ValueError(f"ID 超出范围 (max 0x{max_id:X})")
        return can_id, ext

    def _get_data(self, dlc: int) -> list[int]:
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

    def _on_id_format_changed(self):
        text = self.edit_id.text().strip()
        if not text:
            return
        try:
            if self.id_hex_radio.isChecked():
                # Dec → Hex
                val = int(text)
                self.edit_id.setText(f"0x{val:X}")
            else:
                # Hex → Dec
                if text.lower().startswith("0x"):
                    val = int(text, 16)
                else:
                    val = int(text, 16)
                self.edit_id.setText(str(val))
        except ValueError:
            pass

    def _on_dlc_changed(self, dlc: int):
        for i, edit in enumerate(self.data_edits):
            edit.setEnabled(i < dlc)
            if i >= dlc:
                edit.setText("00")

    # ── Send ─────────────────────────────────────────────────

    def _on_set_bitrate(self):
        label = self.combo_bitrate.currentText()
        bitrate = self.BITRATE_VALUES.get(label, 500000)
        self._send_json(build_set_bitrate(bitrate))

    def _send_json(self, json_str: str):
        self.send_requested.emit(json_str)
        self._log(f">>> {json_str.strip()}")

    def _log(self, msg: str):
        self.history_browser.append(msg)

    # ── Receive feedback ─────────────────────────────────────

    def on_data_received(self, data: str):
        """Parse inbound JSON and provide feedback in the log."""
        lines = self._line_buffer.feed(data)
        for line in lines:
            msg = parse_message(line)
            if isinstance(msg, CommandResponse):
                icon = "✓" if msg.status == "ok" else "✗"
                detail = msg.message if msg.message else ""
                self._log(f"<<< [{icon}] {msg.cmd} {detail}".rstrip())
            elif isinstance(msg, CanStatus):
                state = msg.state
                self._is_can_running = (state == "running")
                color = "green" if state == "running" else ("red" if state == "bus_off" else "gray")
                self.lbl_can_status.setText(f"CAN: {state}")
                self.lbl_can_status.setStyleSheet(f"color: {color}; font-weight: bold;")
                self._log(f"<<< CAN状态: {state} tx_err={msg.tx_errors} rx_err={msg.rx_errors}")

    # ── Helpers ──────────────────────────────────────────────

    def _set_cell(self, row: int, col: int, text: str,
                  align: Qt.AlignmentFlag = Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter):
        item = QTableWidgetItem(text)
        item.setTextAlignment(align)
        self.periodic_table.setItem(row, col, item)
