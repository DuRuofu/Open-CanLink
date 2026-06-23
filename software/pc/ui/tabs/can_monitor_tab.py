"""
CAN Monitor Tab - Displays received CAN frames in a table.
"""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTableWidget, QTableWidgetItem,
    QPushButton, QLineEdit, QLabel, QHeaderView, QCheckBox, QFileDialog,
    QAbstractItemView,
)
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFont, QColor

from core.can_protocol import LineBuffer, CanFrame, parse_message

MAX_ROWS = 1000


class CanMonitorTab(QWidget):
    """Tab displaying received CAN frames in a scrollable table."""

    # Signal emitted when a CAN frame is received (for status bar etc.)
    frame_received = Signal(object)

    def __init__(self):
        super().__init__()
        self._line_buffer = LineBuffer()
        self._paused = False
        self._frame_count = 0
        self._filter_id: int | None = None
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # Toolbar
        toolbar = QHBoxLayout()

        self.btn_pause = QPushButton("暂停")
        self.btn_pause.setCheckable(True)
        self.btn_pause.clicked.connect(self._on_pause)
        toolbar.addWidget(self.btn_pause)

        self.btn_clear = QPushButton("清空")
        self.btn_clear.clicked.connect(self._on_clear)
        toolbar.addWidget(self.btn_clear)

        self.btn_save = QPushButton("保存 CSV")
        self.btn_save.clicked.connect(self._on_save)
        toolbar.addWidget(self.btn_save)

        toolbar.addStretch()

        toolbar.addWidget(QLabel("过滤 ID:"))
        self.edit_filter = QLineEdit()
        self.edit_filter.setPlaceholderText("0x123 or 291 (空=全部)")
        self.edit_filter.setMaximumWidth(150)
        self.edit_filter.textChanged.connect(self._on_filter_changed)
        toolbar.addWidget(self.edit_filter)

        self.chk_hex = QCheckBox("Hex")
        self.chk_hex.setChecked(True)
        toolbar.addWidget(self.chk_hex)

        layout.addLayout(toolbar)

        # Table
        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(
            ["#", "时间戳", "ID", "类型", "DLC", "数据"]
        )
        self.table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Interactive)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.setColumnWidth(0, 50)   # counter
        self.table.setColumnWidth(1, 100)  # timestamp
        self.table.setColumnWidth(2, 80)   # ID
        self.table.setColumnWidth(3, 45)   # Type
        self.table.setColumnWidth(4, 40)   # DLC
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.setFont(QFont("Consolas", 10))
        self.table.verticalHeader().setVisible(False)
        layout.addWidget(self.table)

        # Status bar
        self.lbl_status = QLabel("就绪 - 等待数据...")
        layout.addWidget(self.lbl_status)

    def on_data_received(self, data: str):
        """Called by MainWindow when serial data arrives."""
        lines = self._line_buffer.feed(data)

        for line in lines:
            msg = parse_message(line)
            if isinstance(msg, CanFrame):
                self._add_frame(msg)

    def _add_frame(self, frame: CanFrame):
        """Add a received CAN frame to the table."""
        if self._paused:
            return

        # Filter by ID
        if self._filter_id is not None and frame.id != self._filter_id:
            return

        self._frame_count += 1

        # Trim old rows
        while self.table.rowCount() >= MAX_ROWS:
            self.table.removeRow(0)

        row = self.table.rowCount()
        self.table.insertRow(row)

        # Counter
        self._set_item(row, 0, str(self._frame_count))

        # Timestamp
        ts = frame.timestamp_ms
        self._set_item(row, 1, f"{ts // 1000}.{ts % 1000:03d}")

        # ID
        if self.chk_hex.isChecked():
            id_str = f"0x{frame.id:X}"
        else:
            id_str = str(frame.id)
        self._set_item(row, 2, id_str)

        # Type
        self._set_item(row, 3, "EXT" if frame.ext else "STD")

        # DLC
        self._set_item(row, 4, str(frame.dlc))

        # Data
        data_str = " ".join(f"{b:02X}" for b in frame.data[:frame.dlc])
        self._set_item(row, 5, data_str)

        # Auto-scroll
        self.table.scrollToBottom()

        # Update status
        self.lbl_status.setText(
            f"帧数: {self._frame_count} | "
            f"最后: ID=0x{frame.id:X} DLC={frame.dlc}"
        )

        self.frame_received.emit(frame)

    def _set_item(self, row: int, col: int, text: str):
        item = QTableWidgetItem(text)
        item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
        self.table.setItem(row, col, item)

    def _on_pause(self, checked: bool):
        self._paused = checked
        self.btn_pause.setText("继续" if checked else "暂停")

    def _on_clear(self):
        self.table.setRowCount(0)
        self._frame_count = 0
        self.lbl_status.setText("已清空")

    def _on_save(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "保存 CSV", "", "CSV Files (*.csv)"
        )
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.write("#,Timestamp,ID,Type,DLC,Data\n")
            for row in range(self.table.rowCount()):
                cols = []
                for col in range(6):
                    item = self.table.item(row, col)
                    cols.append(item.text() if item else "")
                f.write(",".join(cols) + "\n")
        self.lbl_status.setText(f"已保存到 {path}")

    def _on_filter_changed(self, text: str):
        text = text.strip()
        if not text:
            self._filter_id = None
            return
        try:
            if text.startswith("0x") or text.startswith("0X"):
                self._filter_id = int(text, 16)
            else:
                self._filter_id = int(text)
        except ValueError:
            self._filter_id = None
