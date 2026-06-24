"""
Filter Tab — Configure CAN hardware acceptance filters.
"""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QPushButton, QTableWidget, QTableWidgetItem, QHeaderView,
    QAbstractItemView, QLineEdit, QLabel, QRadioButton, QButtonGroup,
    QMessageBox,
)
import json

from PySide6.QtCore import Qt, Signal, QSettings

from core.can_protocol import build_set_filter


class FilterTab(QWidget):
    """Hardware CAN filter configuration."""

    send_requested = Signal(str)

    def __init__(self):
        super().__init__()
        self._setup_ui()
        self._load()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # ── Add filter form ──
        add_group = QGroupBox("添加过滤规则")
        add_layout = QHBoxLayout(add_group)

        add_layout.addWidget(QLabel("ID:"))
        self.edit_fid = QLineEdit("0x100")
        self.edit_fid.setMaximumWidth(100)
        add_layout.addWidget(self.edit_fid)

        add_layout.addWidget(QLabel("Mask:"))
        self.edit_fmask = QLineEdit("0x7F0")
        self.edit_fmask.setMaximumWidth(100)
        add_layout.addWidget(self.edit_fmask)

        self.radio_fstd = QRadioButton("STD")
        self.radio_fstd.setChecked(True)
        self.radio_fext = QRadioButton("EXT")
        fg = QButtonGroup(self)
        fg.addButton(self.radio_fstd)
        fg.addButton(self.radio_fext)
        add_layout.addWidget(self.radio_fstd)
        add_layout.addWidget(self.radio_fext)

        self.btn_add_filter = QPushButton("添加")
        self.btn_add_filter.clicked.connect(self._add_filter)
        add_layout.addWidget(self.btn_add_filter)
        add_layout.addStretch()
        layout.addWidget(add_group)

        # ── Filter list ──
        self.table = QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(["ID", "Mask", "类型"])
        h = self.table.horizontalHeader()
        h.setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        self.table.setAlternatingRowColors(True)
        layout.addWidget(self.table)

        # ── Actions ──
        bar = QHBoxLayout()
        self.btn_del_sel = QPushButton("移除选中")
        self.btn_del_sel.clicked.connect(self._del_selected)
        bar.addWidget(self.btn_del_sel)
        self.btn_clear_all = QPushButton("清除全部")
        self.btn_clear_all.clicked.connect(self._clear_all)
        bar.addWidget(self.btn_clear_all)
        bar.addStretch()
        self.btn_apply = QPushButton("应用到设备")
        self.btn_apply.clicked.connect(self._apply)
        bar.addWidget(self.btn_apply)
        layout.addLayout(bar)

        # Hint
        layout.addWidget(QLabel("过滤规则: 接收 (CAN_ID & Mask) == (ID & Mask) 的帧"))

    def _add_filter(self):
        try:
            fid = self._parse(self.edit_fid.text())
            mask = self._parse(self.edit_fmask.text())
        except ValueError:
            return

        ext = self.radio_fext.isChecked()
        row = self.table.rowCount()
        self.table.insertRow(row)
        self._cell(row, 0, f"0x{fid:X}")
        self._cell(row, 1, f"0x{mask:X}")
        self._cell(row, 2, "EXT" if ext else "STD", Qt.AlignmentFlag.AlignCenter)
        self._save()

    def _del_selected(self):
        rows = sorted({i.row() for i in self.table.selectedIndexes()}, reverse=True)
        for r in rows:
            self.table.removeRow(r)
        self._save()

    def _apply(self):
        filters = []
        for row in range(self.table.rowCount()):
            fid = self._parse(self.table.item(row, 0).text())
            mask = self._parse(self.table.item(row, 1).text())
            ext = self.table.item(row, 2).text() == "EXT"
            filters.append({"id": fid, "mask": mask, "ext": ext})
        if filters:
            self.send_requested.emit(build_set_filter(filters))

    def _parse(self, text: str) -> int:
        t = text.strip()
        if t.lower().startswith("0x"):
            return int(t, 16)
        return int(t)

    def _cell(self, row, col, text, align=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter):
        item = QTableWidgetItem(text)
        item.setTextAlignment(align)
        self.table.setItem(row, col, item)

    def set_connected(self, connected: bool):
        self.btn_add_filter.setEnabled(connected)
        self.btn_apply.setEnabled(connected)

    def _clear_all(self):
        self.table.setRowCount(0)
        self._save()

    def _save(self):
        s = QSettings("esp-can-link", "Filters")
        items = []
        for r in range(self.table.rowCount()):
            items.append({
                "id": self.table.item(r, 0).text(),
                "mask": self.table.item(r, 1).text(),
                "ext": self.table.item(r, 2).text() == "EXT",
            })
        s.setValue("filters", json.dumps(items))

    def _load(self):
        s = QSettings("esp-can-link", "Filters")
        raw = s.value("filters", "")
        if not raw:
            return
        try:
            for it in json.loads(raw):
                fid = self._parse(it["id"])
                mask = self._parse(it["mask"])
                ext = it.get("ext", False)
                row = self.table.rowCount()
                self.table.insertRow(row)
                self._cell(row, 0, f"0x{fid:X}")
                self._cell(row, 1, f"0x{mask:X}")
                self._cell(row, 2, "EXT" if ext else "STD", Qt.AlignmentFlag.AlignCenter)
        except (json.JSONDecodeError, KeyError):
            pass
