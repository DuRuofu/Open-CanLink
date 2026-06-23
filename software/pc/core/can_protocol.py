"""
CAN Protocol - JSON line-delimited protocol codec.

Implements the protocol defined in docs/protocol.md.
Handles line buffering, JSON parsing, and command building.
"""

import json
from dataclasses import dataclass, field


# ---- Data Models ----


@dataclass
class CanFrame:
    """A received or sent CAN frame."""
    id: int
    ext: bool
    dlc: int
    data: list[int]
    timestamp_ms: int = 0


@dataclass
class CanStatus:
    """CAN bus status from device."""
    state: str = "stopped"  # "stopped", "running", "bus_off"
    tx_errors: int = 0
    rx_errors: int = 0
    bus_off: bool = False


@dataclass
class CommandResponse:
    """Response to a command."""
    cmd: str = ""
    status: str = ""  # "ok" or "error"
    message: str = ""


@dataclass
class DeviceInfo:
    """Device identification info."""
    firmware: str = ""
    version: str = ""
    hw: str = ""


# ---- Line Buffer ----


class LineBuffer:
    """
    Accumulates bytes from serial reads and yields complete
    newline-terminated lines.
    """

    def __init__(self):
        self._buffer = bytearray()

    def feed(self, data: str | bytes) -> list[str]:
        """
        Feed raw data. Returns a list of complete lines (strings).
        Incomplete last line is kept in the buffer.
        """
        if isinstance(data, str):
            data = data.encode("utf-8", errors="ignore")
        self._buffer.extend(data)
        lines = []
        while b"\n" in self._buffer:
            idx = self._buffer.index(b"\n")
            line = self._buffer[:idx]
            self._buffer = self._buffer[idx + 1:]
            # Strip \r if present
            if line.endswith(b"\r"):
                line = line[:-1]
            try:
                lines.append(line.decode("utf-8", errors="ignore").strip())
            except Exception:
                pass
        return [l for l in lines if l]

    def reset(self):
        """Clear the buffer."""
        self._buffer.clear()


# ---- Message Parsing ----


def parse_message(line: str):
    """
    Parse a JSON line from the device.
    Returns one of: CanFrame, CanStatus, CommandResponse, DeviceInfo, or None.
    """
    try:
        obj = json.loads(line)
    except (json.JSONDecodeError, TypeError):
        return None

    msg_type = obj.get("type")

    if msg_type == "rx":
        return CanFrame(
            id=obj.get("id", 0),
            ext=obj.get("ext", False),
            dlc=obj.get("dlc", 0),
            data=obj.get("data", []),
            timestamp_ms=obj.get("timestamp_ms", 0),
        )
    elif msg_type == "status":
        return CanStatus(
            state=obj.get("state", "unknown"),
            tx_errors=obj.get("tx_errors", 0),
            rx_errors=obj.get("rx_errors", 0),
            bus_off=obj.get("bus_off", False),
        )
    elif msg_type == "response":
        return CommandResponse(
            cmd=obj.get("cmd", ""),
            status=obj.get("status", ""),
            message=obj.get("message", ""),
        )
    elif msg_type == "info":
        return DeviceInfo(
            firmware=obj.get("firmware", ""),
            version=obj.get("version", ""),
            hw=obj.get("hw", ""),
        )

    return None


# ---- Command Builders (Host -> Device) ----


def _to_json(obj: dict) -> str:
    return json.dumps(obj, separators=(",", ":")) + "\n"


def build_can_start() -> str:
    return _to_json({"cmd": "can_start"})


def build_can_stop() -> str:
    return _to_json({"cmd": "can_stop"})


def build_send(can_id: int, ext: bool, data: list[int]) -> str:
    return _to_json({
        "cmd": "send",
        "id": can_id,
        "ext": ext,
        "data": data,
    })


def build_periodic_start(can_id: int, ext: bool,
                         data: list[int], period_ms: int) -> str:
    return _to_json({
        "cmd": "periodic_start",
        "id": can_id,
        "ext": ext,
        "data": data,
        "period_ms": period_ms,
    })


def build_periodic_stop(can_id: int) -> str:
    return _to_json({"cmd": "periodic_stop", "id": can_id})


def build_set_bitrate(bitrate: int) -> str:
    return _to_json({"cmd": "set_bitrate", "bitrate": bitrate})


def build_set_filter(filters: list[dict]) -> str:
    return _to_json({"cmd": "set_filter", "filter": filters})


def build_get_status() -> str:
    return _to_json({"cmd": "get_status"})


def build_get_info() -> str:
    return _to_json({"cmd": "get_info"})
