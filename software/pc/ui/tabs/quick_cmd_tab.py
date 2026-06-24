"""
Quick Command Tab — One-click send of saved CAN commands.
"""

import json
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QTableWidget, QTableWidgetItem, QHeaderView,
    QAbstractItemView, QLineEdit,
)
from PySide6.QtCore import Qt, Signal, QSettings


class QuickCmdTab(QWidget):
    send_requested = Signal(str)

    def __init__(self):
        super().__init__()
        self._cmd_count = 0
        self._setup_ui()
        self._load()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        bar = QHBoxLayout()
        self.btn_del = QPushButton("删除选中")
        self.btn_del.clicked.connect(self._del_cmd)
        bar.addWidget(self.btn_del)
        bar.addStretch()
        layout.addLayout(bar)

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["#", "ID", "数据", "备注", "操作"])
        h = self.table.horizontalHeader()
        h.setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.table.setColumnWidth(0, 35)
        self.table.setColumnWidth(4, 55)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        self.table.setAlternatingRowColors(True)
        layout.addWidget(self.table)

    def add_from_can_tab(self, can_id: int, ext: bool, dlc: int,
                          data: list[int], period_ms: int = 0):
        self._cmd_count += 1
        name = str(self._cmd_count)
        id_str = f"0x{can_id:X}"
        type_str = "EXT" if ext else "STD"
        data_str = " ".join(f"{b:02X}" for b in data[:dlc]) if dlc > 0 else "--"
        cmd = {"cmd": "send", "id": can_id, "ext": ext, "data": data[:dlc]}
        json_str = json.dumps(cmd, separators=(",", ":")) + "\n"
        self._add_row(name, id_str + " " + type_str, data_str, json_str, "")
        self._save()

    def _add_row(self, name: str, id_str: str, data_str: str, json_str: str, remark: str):
        row = self.table.rowCount()
        self.table.insertRow(row)
        self._cell(row, 0, name, Qt.AlignmentFlag.AlignCenter)
        self._cell(row, 1, id_str, Qt.AlignmentFlag.AlignCenter)
        self._cell(row, 2, data_str)
        # Editable remark
        remark_edit = QLineEdit(remark)
        remark_edit.setPlaceholderText("备注")
        remark_edit.textChanged.connect(lambda t, r=row: self._on_remark(r, t))
        self.table.setCellWidget(row, 3, remark_edit)
        # Send button
        btn = QPushButton("▶")
        btn.clicked.connect(lambda: self.send_requested.emit(json_str))
        self.table.setCellWidget(row, 4, btn)
        # Store JSON
        self.table.item(row, 0).setData(Qt.ItemDataRole.UserRole, json_str)

    def _on_remark(self, row: int, text: str):
        self._save()

    def _del_cmd(self):
        rows = sorted({i.row() for i in self.table.selectedIndexes()}, reverse=True)
        for r in rows:
            self.table.removeRow(r)
        self._save()

    def _cell(self, row, col, text, align=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter):
        item = QTableWidgetItem(text)
        item.setTextAlignment(align)
        self.table.setItem(row, col, item)

    def set_connected(self, connected: bool):
        self.btn_del.setEnabled(connected)

    # ── Persistence ──────────────────────────────────────

    def _save(self):
        s = QSettings("esp-can-link", "QuickCmds")
        items = []
        for r in range(self.table.rowCount()):
            name = self.table.item(r, 0).text() if self.table.item(r, 0) else ""
            remark_w = self.table.cellWidget(r, 3)
            remark = remark_w.text() if isinstance(remark_w, QLineEdit) else ""
            json_str = self.table.item(r, 0).data(Qt.ItemDataRole.UserRole) if self.table.item(r, 0) else ""
            items.append({"name": name, "remark": remark, "json": json_str})
        s.setValue("commands", json.dumps(items))

    def _load(self):
        s = QSettings("esp-can-link", "QuickCmds")
        raw = s.value("commands", "")
        if not raw:
            return
        try:
            items = json.loads(raw)
            self._cmd_count = max(int(it.get("name", 0)) for it in items) if items else 0
            for it in items:
                cmd = json.loads(it["json"] + "\n") if not it["json"].endswith("\n") else json.loads(it["json"])
                cid = cmd.get("id", 0)
                ext = cmd.get("ext", False)
                dlc = len(cmd.get("data", []))
                data = cmd.get("data", [])
                id_str = f"0x{cid:X} " + ("EXT" if ext else "STD")
                data_str = " ".join(f"{b:02X}" for b in data[:dlc]) if dlc > 0 else "--"
                self._add_row(it["name"], id_str, data_str, it["json"], it.get("remark", ""))
        except (json.JSONDecodeError, KeyError):
            pass
