"""Serial Manager - 串口通信管理，线程安全"""
import threading
from PySide6.QtCore import QObject, Signal, QThread, QMutex


class SerialWorker(QObject):
    """串口工作线程，负责底层串口通信"""

    connected = Signal()
    disconnected = Signal()
    device_lost = Signal()  # 设备意外断开（拔线、复位等）
    error_occurred = Signal(str)
    data_received = Signal(str)
    data_sent = Signal(str)

    def __init__(self):
        super().__init__()
        self.serial_port = None
        self.is_running = False
        self._lock = threading.Lock()
        self._read_error_count = 0

    def open_serial(self, port_name, baud_rate):
        """打开串口"""
        import serial
        try:
            self.serial_port = serial.Serial(
                port=port_name,
                baudrate=int(baud_rate),
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1
            )
            self.is_running = True
            self._read_error_count = 0
            self.connected.emit()
        except Exception as e:
            self.error_occurred.emit(f"串口打开失败: {str(e)}")

    def close_serial(self):
        """关闭串口"""
        with self._lock:
            self.is_running = False
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.close()
        self.disconnected.emit()

    def send_data(self, data):
        """发送数据"""
        with self._lock:
            if not self.is_running or not self.serial_port or not self.serial_port.is_open:
                return False
            try:
                self.serial_port.write(data.encode('utf-8'))
            except Exception as e:
                self.error_occurred.emit(f"数据发送失败: {str(e)}")
                return False
        self.data_sent.emit(data.strip())
        return True

    def read_data(self):
        """读取串口数据（由定时器调用）"""
        data = None
        with self._lock:
            if not self.is_running or not self.serial_port or not self.serial_port.is_open:
                return
            try:
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    self._read_error_count = 0
            except Exception as e:
                self._read_error_count += 1
                # 连续出错达到阈值 → 设备已断开，通知上层
                if self._read_error_count >= 5:
                    self.is_running = False
                    self.device_lost.emit()
                elif self._read_error_count == 1:
                    # 仅第一次记录错误，避免日志刷屏
                    self.error_occurred.emit(f"数据读取失败: {str(e)}")
        if data:
            decoded = data.decode('utf-8', errors='ignore')
            self.data_received.emit(decoded)


class SerialManager(QObject):
    """串口通信管理器 - 对外提供统一接口"""

    connected = Signal()
    disconnected = Signal()
    error_occurred = Signal(str)
    data_received = Signal(str)
    data_sent = Signal(str)

    def __init__(self):
        super().__init__()
        self._worker = None
        self._thread = None
        self._is_connected = False
        self._timer = None

    @property
    def is_connected(self):
        return self._is_connected

    def connect(self, port_name, baud_rate, log_manager=None):
        """连接串口"""
        if self._is_connected:
            return False

        self._thread = QThread()
        self._worker = SerialWorker()
        self._worker.moveToThread(self._thread)

        self._worker.connected.connect(self._on_connected)
        self._worker.disconnected.connect(self._on_disconnected)
        self._worker.device_lost.connect(self._on_device_lost)
        self._worker.error_occurred.connect(lambda e: self._on_error(e, log_manager))
        self._worker.data_received.connect(lambda d: self._on_data_received(d, log_manager))
        self._worker.data_sent.connect(lambda d: self._on_sent(d, log_manager))

        from PySide6.QtCore import QTimer
        self._timer = QTimer()
        self._timer.timeout.connect(self._worker.read_data)

        self._thread.started.connect(
            lambda: self._worker.open_serial(port_name, baud_rate)
        )
        self._thread.start()
        self._timer.start(10)
        return True

    def disconnect(self):
        """断开串口"""
        if self._worker:
            self._worker.close_serial()
        if self._timer:
            self._timer.stop()
        if self._thread:
            self._thread.quit()
            if not self._thread.wait(3000):
                self._thread.terminate()
                self._thread.wait(1000)
        # Clean up Qt objects to prevent leaks on reconnect
        if self._timer:
            self._timer.deleteLater()
            self._timer = None
        if self._worker:
            self._worker.deleteLater()
            self._worker = None
        if self._thread:
            self._thread.deleteLater()
            self._thread = None
        self._is_connected = False

    def send(self, data):
        """发送数据"""
        if self._worker and self._is_connected:
            return self._worker.send_data(data)
        return False

    def _on_connected(self):
        self._is_connected = True
        self.connected.emit()

    def _on_disconnected(self):
        self._is_connected = False
        self.disconnected.emit()

    def _on_device_lost(self):
        """设备意外断开（拔线/复位），自动清理"""
        self._is_connected = False
        if self._timer:
            self._timer.stop()
        self.disconnected.emit()

    def _on_error(self, msg, log_manager):
        self.error_occurred.emit(msg)
        if log_manager:
            log_manager.error(msg)

    def _on_data_received(self, data, log_manager):
        self.data_received.emit(data)
        if log_manager:
            log_manager.recv(data)

    def _on_sent(self, data, log_manager):
        self.data_sent.emit(data)
        if log_manager:
            log_manager.send(data)