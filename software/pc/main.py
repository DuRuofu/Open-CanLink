"""esp-can-link PC 上位机"""
import sys, os, serial.tools.list_ports
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QIcon

from core.serial_manager import SerialManager
from core.log_manager import LogManager
from ui.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("esp-can-link")

    logo_path = os.path.join(os.path.dirname(__file__), "logo.png")
    if os.path.exists(logo_path):
        app.setWindowIcon(QIcon(logo_path))

    serial_manager = SerialManager()
    log_manager = LogManager()

    window = MainWindow(serial_manager, log_manager)
    window.show()

    # 自动扫描 + 恢复上次配置
    ports = [p.device for p in serial.tools.list_ports.comports()]
    window.serial_panel.set_port_list(ports)
    window.serial_panel.restore_settings()

    serial_manager.connected.connect(lambda: window.serial_panel.save_settings())
    serial_manager.disconnected.connect(lambda: window.serial_panel.save_settings())

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
