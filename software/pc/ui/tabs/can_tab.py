"""
CAN Tab — Combined CAN monitor + send in a single page.

Top: CAN frame monitor with frame rate
Bottom: CAN frame send area with clickable history and periodic list
"""

from datetime import datetime

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QLineEdit, QPushButton, QComboBox,
    QSpinBox, QRadioButton, QButtonGroup, QTableWidget, QTableWidgetItem,
    QHeaderView, QAbstractItemView, QMessageBox, QSplitter,
)
from PySide6.QtCore import Qt, Signal, QTimer
from PySide6.QtGui import QFont, QFontDatabase, QRegularExpressionValidator

from core.can_protocol import (
    LineBuffer, parse_message, CanFrame, CanStatus, CommandResponse, DeviceInfo,
    build_send, build_periodic_start, build_periodic_stop,
    build_set_bitrate, build_get_status, build_get_info,
)

MAX_ROWS = 2000
MAX_PERIODIC = 4
BITRATES = ["125K", "250K", "500K", "1M"]
BITRATE_VALUES = {"125K": 125000, "250K": 250000, "500K": 500000, "1M": 1000000}


class CanTab(QWidget):
    """Merged CAN monitor + send page."""

    send_requested = Signal(str)
    add_to_quick_cmd = Signal(int, bool, int, list, int)  # id, ext, dlc, data, period_ms

    def __init__(self):
        super().__init__()
        self._line_buffer = LineBuffer()
        self._is_connected = False
        self._active_periodic_ids: set[int] = set()
        self._frame_count = 0
        self._last_sec_count = 0
        self._fps = 0
        self._paused = False
        self._setup_ui()

        # Frame rate timer
        self._fps_timer = QTimer()
        self._fps_timer.timeout.connect(self._update_fps)
        self._fps_timer.start(1000)

    # ── UI Setup ─────────────────────────────────────────

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        splitter = QSplitter(Qt.Orientation.Vertical)

        # ── Top: Monitor + Control ──
        top = QWidget()
        top_layout = QVBoxLayout(top)
        top_layout.setContentsMargins(0, 0, 0, 0)

        # Control bar
        ctrl = QHBoxLayout()

        ctrl.addWidget(QLabel("波特率:"))
        self.combo_bitrate = QComboBox()
        self.combo_bitrate.addItems(BITRATES)
        self.combo_bitrate.setCurrentText("500K")
        ctrl.addWidget(self.combo_bitrate)

        self.btn_set_bitrate = QPushButton("设置")
        self.btn_set_bitrate.clicked.connect(lambda: self._send_json(
            build_set_bitrate(BITRATE_VALUES[self.combo_bitrate.currentText()])))
        ctrl.addWidget(self.btn_set_bitrate)

        self.btn_get_status = QPushButton("状态")
        ctrl.addWidget(self.btn_get_status)
        self.btn_get_status.clicked.connect(lambda: self._send_json(build_get_status()))

        self.btn_get_info = QPushButton("设备信息")
        ctrl.addWidget(self.btn_get_info)
        self.btn_get_info.clicked.connect(lambda: self._send_json(build_get_info()))

        ctrl.addStretch()

        self.btn_pause = QPushButton("暂停")
        self.btn_pause.setCheckable(True)
        self.btn_pause.clicked.connect(lambda c: setattr(self, '_paused', c))
        ctrl.addWidget(self.btn_pause)

        self.btn_clear_mon = QPushButton("清空")
        self.btn_clear_mon.clicked.connect(self._clear_monitor)
        ctrl.addWidget(self.btn_clear_mon)

        self.btn_save_csv = QPushButton("保存")
        self.btn_save_csv.clicked.connect(self._save_csv)
        ctrl.addWidget(self.btn_save_csv)

        self.lbl_fps = QLabel("帧率: --")
        ctrl.addWidget(self.lbl_fps)

        top_layout.addLayout(ctrl)

        # Monitor table
        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(["#", "时间", "ID", "类型", "DLC", "数据"])
        h = self.table.horizontalHeader()
        h.setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        h.setStretchLastSection(True)
        self.table.setColumnWidth(0, 45)
        self.table.setColumnWidth(1, 85)
        self.table.setColumnWidth(2, 70)
        self.table.setColumnWidth(3, 38)
        self.table.setColumnWidth(4, 35)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setVisible(False)
        fixed_font = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
        fixed_font.setPointSize(10)
        self.table.setFont(fixed_font)
        self.table.doubleClicked.connect(self._on_row_double_clicked)
        top_layout.addWidget(self.table)

        splitter.addWidget(top)

        # ── Bottom: Send area ──
        bottom = QWidget()
        bottom_layout = QVBoxLayout(bottom)
        bottom_layout.setContentsMargins(0, 2, 0, 0)
        bottom_layout.setSpacing(4)

        send_group = QGroupBox("CAN 帧发送")
        send_layout = QGridLayout(send_group)
        send_layout.setContentsMargins(4, 4, 4, 2)
        send_layout.setVerticalSpacing(3)

        # Row 0: ID [Hex][Dec] [STD][EXT] DLC:[▾]
        id_row = QHBoxLayout()
        id_row.addWidget(QLabel("ID:"))
        self.edit_id = QLineEdit("0x123")
        self.edit_id.setMaximumWidth(100)
        id_row.addWidget(self.edit_id)

        self.radio_hex = QRadioButton("Hex")
        self.radio_hex.setChecked(True)
        self.radio_dec = QRadioButton("Dec")
        self._id_fmt_group = QButtonGroup(self)
        self._id_fmt_group.addButton(self.radio_hex)
        self._id_fmt_group.addButton(self.radio_dec)
        self._id_fmt_group.buttonClicked.connect(self._on_id_format)
        id_row.addWidget(self.radio_hex)
        id_row.addWidget(self.radio_dec)

        id_row.addSpacing(8)
        self.radio_std = QRadioButton("STD")
        self.radio_std.setChecked(True)
        self.radio_ext = QRadioButton("EXT")
        self._type_group = QButtonGroup(self)
        self._type_group.addButton(self.radio_std)
        self._type_group.addButton(self.radio_ext)
        id_row.addWidget(self.radio_std)
        id_row.addWidget(self.radio_ext)

        id_row.addSpacing(8)
        id_row.addWidget(QLabel("DLC:"))
        self.spin_dlc = QSpinBox()
        self.spin_dlc.setRange(0, 8)
        self.spin_dlc.setValue(4)
        self.spin_dlc.setMaximumWidth(50)
        self.spin_dlc.valueChanged.connect(self._on_dlc)
        id_row.addWidget(self.spin_dlc)
        id_row.addStretch()
        send_layout.addLayout(id_row, 0, 0, 1, 3)

        # Row 1: Data + buttons in one compact row
        data_btn_row = QHBoxLayout()
        data_btn_row.addWidget(QLabel("Data:"))
        self.edit_data = QLineEdit("01 02 03 04 00 00 00 00")
        self.edit_data.setPlaceholderText("01 02 03 04 ... 空格分隔")
        data_btn_row.addWidget(self.edit_data, stretch=1)

        self.btn_send = QPushButton("发送一次")
        self.btn_send.clicked.connect(self._send_once)
        data_btn_row.addWidget(self.btn_send)

        data_btn_row.addWidget(QLabel("周期:"))
        self.edit_period = QLineEdit("100")
        self.edit_period.setMaximumWidth(50)
        data_btn_row.addWidget(self.edit_period)
        data_btn_row.addWidget(QLabel("ms"))

        self.btn_add_list = QPushButton("添加到列表")
        self.btn_add_list.clicked.connect(self._add_to_list)
        data_btn_row.addWidget(self.btn_add_list)

        self.btn_save_quick = QPushButton("存快捷")
        self.btn_save_quick.clicked.connect(self._save_to_quick)
        data_btn_row.addWidget(self.btn_save_quick)

        self.lbl_send_count = QLabel("已发: 0")
        data_btn_row.addWidget(self.lbl_send_count)
        send_layout.addLayout(data_btn_row, 1, 0, 1, 3)

        bottom_layout.addWidget(send_group)

        # Periodic list
        list_group = QGroupBox("周期发送列表")
        list_layout = QVBoxLayout(list_group)
        list_layout.setContentsMargins(4, 4, 4, 2)
        list_layout.setSpacing(2)
        self.periodic_table = QTableWidget(0, 5)
        self.periodic_table.setHorizontalHeaderLabels(["ID", "类型", "数据", "周期", "操作"])
        h2 = self.periodic_table.horizontalHeader()
        h2.setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        h2.setStretchLastSection(True)
        self.periodic_table.setColumnWidth(0, 70)
        self.periodic_table.setColumnWidth(1, 38)
        self.periodic_table.setColumnWidth(2, 140)
        self.periodic_table.setColumnWidth(3, 55)
        self.periodic_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.periodic_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.periodic_table.verticalHeader().setVisible(False)
        self.periodic_table.setMaximumHeight(200)
        list_layout.addWidget(self.periodic_table)

        list_bar = QHBoxLayout()
        self.btn_stop_all = QPushButton("全部停止")
        self.btn_stop_all.clicked.connect(self._stop_all)
        list_bar.addWidget(self.btn_stop_all)
        self.btn_start_all = QPushButton("全部启动")
        self.btn_start_all.clicked.connect(self._start_all)
        list_bar.addWidget(self.btn_start_all)
        self.btn_remove_sel = QPushButton("移除选中")
        self.btn_remove_sel.clicked.connect(self._remove_selected)
        list_bar.addWidget(self.btn_remove_sel)
        list_bar.addStretch()
        self.lbl_slots = QLabel(f"0/{MAX_PERIODIC}")
        list_bar.addWidget(self.lbl_slots)
        list_layout.addLayout(list_bar)
        bottom_layout.addWidget(list_group)

        splitter.addWidget(bottom)
        splitter.setSizes([350, 400])
        layout.addWidget(splitter)

        # Init
        self._on_dlc(4)

    # ── Connection state ─────────────────────────────────

    def set_connected(self, connected: bool):
        self._is_connected = connected
        for w in [self.btn_send, self.btn_add_list, self.btn_save_quick, self.btn_set_bitrate,
                   self.btn_get_status, self.btn_get_info,
                   self.btn_stop_all, self.btn_start_all, self.btn_remove_sel]:
            w.setEnabled(connected)
        if not connected:
            self._active_periodic_ids.clear()
            self._update_table_statuses()

    # ── Monitor ──────────────────────────────────────────

    def on_data_received(self, data: str):
        lines = self._line_buffer.feed(data)
        for line in lines:
            msg = parse_message(line)
            if isinstance(msg, CanFrame):
                self._add_frame(msg)
            elif isinstance(msg, CommandResponse):
                icon = "✓" if msg.status == "ok" else "✗"
                self._log(f"<<< [{icon}] {msg.cmd} {msg.message}".rstrip())
                if msg.cmd == "can_start" and msg.status == "ok":
                    self._log("<<< CAN 已启动")
            elif isinstance(msg, CanStatus):
                color = "green" if msg.state == "running" else ("red" if msg.state == "bus_off" else "gray")
                self._log(f"<<< CAN: <span style='color:{color}'>{msg.state}</span> tx_err={msg.tx_errors} rx_err={msg.rx_errors}")
            elif isinstance(msg, DeviceInfo):
                self._log(f"<<< 设备: {msg.firmware} v{msg.version} ({msg.hw})")

    def _add_frame(self, frame: CanFrame):
        if self._paused:
            return

        self._frame_count += 1
        self._last_sec_count += 1

        while self.table.rowCount() >= MAX_ROWS:
            self.table.removeRow(0)

        row = self.table.rowCount()
        self.table.insertRow(row)

        now = datetime.now()
        self._set_cell(row, 0, str(self._frame_count), Qt.AlignmentFlag.AlignCenter)
        self._set_cell(row, 1, now.strftime("%H:%M:%S.%f")[:-3], Qt.AlignmentFlag.AlignCenter)
        self._set_cell(row, 2, f"0x{frame.id:X}", Qt.AlignmentFlag.AlignCenter)
        self._set_cell(row, 3, "EXT" if frame.ext else "STD", Qt.AlignmentFlag.AlignCenter)
        self._set_cell(row, 4, str(frame.dlc), Qt.AlignmentFlag.AlignCenter)
        self._set_cell(row, 5, " ".join(f"{b:02X}" for b in frame.data[:frame.dlc]) if frame.dlc > 0 else "--")
        self._set_cell(row, 0, str(self._frame_count), Qt.AlignmentFlag.AlignCenter)

        # Smart scroll
        vbar = self.table.verticalScrollBar()
        if vbar.value() >= vbar.maximum() - 10:
            self.table.scrollToBottom()

    def _on_row_double_clicked(self, idx):
        """Fill send area from monitor row."""
        row = idx.row()
        id_text = self.table.item(row, 2)
        type_text = self.table.item(row, 3)
        dlc_text = self.table.item(row, 4)
        data_text = self.table.item(row, 5)
        if not all([id_text, type_text, dlc_text, data_text]):
            return

        self.edit_id.setText(id_text.text())
        self.radio_hex.setChecked(True)
        if type_text.text() == "EXT":
            self.radio_ext.setChecked(True)
        else:
            self.radio_std.setChecked(True)
        dlc = int(dlc_text.text())
        self.spin_dlc.setValue(dlc)
        self.edit_data.setText(data_text.text())

    def _clear_monitor(self):
        self.table.setRowCount(0)
        self._frame_count = 0

    def _update_fps(self):
        self._fps = self._last_sec_count
        self._last_sec_count = 0
        self.lbl_fps.setText(f"帧率: {self._fps} fps | 总帧: {self._frame_count}")

    # ── Save ────────────────────────────────────────────

    def _save_csv(self):
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getSaveFileName(self, "保存 CSV", "", "CSV (*.csv)")
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.write("#,Time,ID,Type,DLC,Data\n")
            for row in range(self.table.rowCount()):
                cols = [self.table.item(row, c).text() if self.table.item(row, c) else ""
                        for c in range(6)]
                f.write(",".join(cols) + "\n")

    # ── Send ─────────────────────────────────────────────

    _send_count = 0

    def _send_once(self):
        try:
            can_id, ext = self._parse_id()
            dlc = self.spin_dlc.value()
            data = self._parse_data(dlc)
        except ValueError as e:
            self._log(f"错误: {e}")
            return
        self._send_json(build_send(can_id, ext, data))

    def _save_to_quick(self):
        try:
            can_id, ext = self._parse_id()
            dlc = self.spin_dlc.value()
            data = self._parse_data(dlc)
            period_ms = int(self.edit_period.text() or "0")
        except ValueError as e:
            self._log(f"错误: {e}")
            return
        self.add_to_quick_cmd.emit(can_id, ext, dlc, data, period_ms)

    def _add_to_list(self):
        try:
            can_id, ext = self._parse_id()
            dlc = self.spin_dlc.value()
            data = self._parse_data(dlc)
            period_ms = int(self.edit_period.text() or "100")
            if period_ms < 1:
                raise ValueError("周期 >= 1 ms")
        except ValueError as e:
            self._log(f"错误: {e}")
            return

        if self.periodic_table.rowCount() >= MAX_PERIODIC:
            QMessageBox.warning(self, "槽位已满", f"最多 {MAX_PERIODIC} 个")
            return

        # Check duplicate
        for r in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(r, 0)
            if item and item.data(Qt.ItemDataRole.UserRole):
                meta = item.data(Qt.ItemDataRole.UserRole)
                if meta["can_id"] == can_id:
                    rpl = QMessageBox.question(self, "重复", f"ID 0x{can_id:X} 已存在，覆盖？")
                    if rpl != QMessageBox.StandardButton.Yes:
                        return
                    old_id = meta["can_id"]
                    self.periodic_table.removeRow(r)
                    self._active_periodic_ids.discard(old_id)
                    break

        self._add_periodic_row(can_id, ext, dlc, data, period_ms, active=True)

    def _parse_id(self) -> tuple[int, bool]:
        text = self.edit_id.text().strip()
        if self.radio_hex.isChecked():
            if text.lower().startswith("0x"):
                can_id = int(text, 16)
            else:
                can_id = int(text, 16) if all(c in "0123456789ABCDEFabcdef" for c in text) else int(text)
        else:
            can_id = int(text)
        ext = self.radio_ext.isChecked()
        max_id = 0x1FFFFFFF if ext else 0x7FF
        if can_id < 0 or can_id > max_id:
            raise ValueError(f"ID 超范围 (max 0x{max_id:X})")
        return can_id, ext

    def _parse_data(self, dlc: int) -> list[int]:
        text = self.edit_data.text().strip()
        # Accept "01 02 03" or "0x01 0x02" or "01,02,03" or "010203"
        import re
        parts = re.split(r'[\s,]+', text)
        parts = [p for p in parts if p]
        if len(parts) == 1 and len(parts[0]) >= 2 and ' ' not in text and ',' not in text:
            # Treat as concatenated hex: "010203"
            hex_str = parts[0]
            parts = [hex_str[i:i+2] for i in range(0, len(hex_str), 2)]

        data = []
        for i in range(min(dlc, len(parts))):
            p = parts[i].strip()
            if p.lower().startswith("0x"):
                p = p[2:]
            val = int(p, 16)
            if val < 0 or val > 255:
                raise ValueError(f"字节 {i} 超范围")
            data.append(val)
        # Pad to dlc
        while len(data) < dlc:
            data.append(0)
        return data

    def _on_id_format(self):
        text = self.edit_id.text().strip()
        if not text:
            return
        # Parse consistently: if text has 0x prefix, always treat as hex
        # Otherwise parse by the OPPOSITE format (converting FROM what it currently is)
        try:
            if text.lower().startswith("0x"):
                val = int(text, 16)
            else:
                # Currently displayed as decimal — parse as decimal
                val = int(text)

            if self.radio_hex.isChecked():
                self.edit_id.setText(f"0x{val:X}")
            else:
                self.edit_id.setText(str(val))
        except ValueError:
            pass

    def _on_dlc(self, dlc: int):
        # Keep existing data, just change how many bytes are used
        pass

    # ── Periodic list ────────────────────────────────────

    def _add_periodic_row(self, can_id, ext, dlc, data, period_ms, active):
        row = self.periodic_table.rowCount()
        self.periodic_table.insertRow(row)
        meta = {"can_id": can_id, "ext": ext, "dlc": dlc, "data": data, "period_ms": period_ms}

        self._set_per_cell(row, 0, f"0x{can_id:X}", Qt.AlignmentFlag.AlignCenter)
        self._set_per_cell(row, 1, "EXT" if ext else "STD", Qt.AlignmentFlag.AlignCenter)
        self._set_per_cell(row, 2, " ".join(f"{b:02X}" for b in data[:dlc]) if dlc > 0 else "--")
        self._set_per_cell(row, 3, str(period_ms), Qt.AlignmentFlag.AlignCenter)
        self.periodic_table.item(row, 0).setData(Qt.ItemDataRole.UserRole, meta)

        btn = QPushButton("停止" if active else "启动")
        btn.clicked.connect(lambda: self._toggle_row(row))
        self.periodic_table.setCellWidget(row, 4, btn)

        if active:
            self._active_periodic_ids.add(can_id)
            self._send_json(build_periodic_start(can_id, ext, data[:dlc], period_ms))

        self._update_slots()

    def _toggle_row(self, row: int):
        item = self.periodic_table.item(row, 0)
        if not item: return
        meta = item.data(Qt.ItemDataRole.UserRole)
        if not meta: return
        cid = meta["can_id"]

        if cid in self._active_periodic_ids:
            self._send_json(build_periodic_stop(cid))
            self._active_periodic_ids.discard(cid)
        else:
            self._send_json(build_periodic_start(cid, meta["ext"], meta["data"][:meta["dlc"]], meta["period_ms"]))
            self._active_periodic_ids.add(cid)

        btn = self.periodic_table.cellWidget(row, 4)
        if isinstance(btn, QPushButton):
            btn.setText("停止" if cid in self._active_periodic_ids else "启动")

    def _update_table_statuses(self):
        for row in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(row, 0)
            if not item: continue
            meta = item.data(Qt.ItemDataRole.UserRole)
            if not meta: continue
            btn = self.periodic_table.cellWidget(row, 4)
            if isinstance(btn, QPushButton):
                btn.setText("停止" if meta["can_id"] in self._active_periodic_ids else "启动")

    def _stop_all(self):
        for row in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(row, 0)
            if not item: continue
            meta = item.data(Qt.ItemDataRole.UserRole)
            if not meta: continue
            if meta["can_id"] in self._active_periodic_ids:
                self._send_json(build_periodic_stop(meta["can_id"]))
                self._active_periodic_ids.discard(meta["can_id"])
        self._update_table_statuses()

    def _start_all(self):
        for row in range(self.periodic_table.rowCount()):
            item = self.periodic_table.item(row, 0)
            if not item: continue
            meta = item.data(Qt.ItemDataRole.UserRole)
            if not meta: continue
            if meta["can_id"] not in self._active_periodic_ids:
                self._send_json(build_periodic_start(meta["can_id"], meta["ext"], meta["data"][:meta["dlc"]], meta["period_ms"]))
                self._active_periodic_ids.add(meta["can_id"])
        self._update_table_statuses()

    def _remove_selected(self):
        rows = set(i.row() for i in self.periodic_table.selectedIndexes())
        for row in sorted(rows, reverse=True):
            item = self.periodic_table.item(row, 0)
            if item:
                meta = item.data(Qt.ItemDataRole.UserRole)
                if meta and meta["can_id"] in self._active_periodic_ids:
                    self._send_json(build_periodic_stop(meta["can_id"]))
                    self._active_periodic_ids.discard(meta["can_id"])
            self.periodic_table.removeRow(row)
        self._update_slots()

    def _update_slots(self):
        self.lbl_slots.setText(f"{self.periodic_table.rowCount()}/{MAX_PERIODIC}")

    # ── History ──────────────────────────────────────────

    def _send_json(self, json_str: str):
        self.send_requested.emit(json_str)
        self._send_count += 1
        self.lbl_send_count.setText(f"已发: {self._send_count}")

    def _log(self, msg: str):
        pass  # 右侧日志面板已显示所有通信，此处不再重复

    # ── Helpers ──────────────────────────────────────────

    def _set_cell(self, row, col, text, align=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter):
        item = QTableWidgetItem(text)
        item.setTextAlignment(align)
        self.table.setItem(row, col, item)

    def _set_per_cell(self, row, col, text, align=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter):
        item = QTableWidgetItem(text)
        item.setTextAlignment(align)
        self.periodic_table.setItem(row, col, item)
