#!/usr/bin/env python3
"""
REII Dashboard Serial Reader — reads groundstation USB-CDC (COM12 @ 115200).

Parses the groundstation's newline-terminated serial output:
  ECHO,<text>
  PONG,<ok>,<ms>
  OK,app_ch=77,2M,PA_MAX,ARC5,ARD500
  ACK,<ok>,payload=<64 hex chars>   <- 32-byte ACK-payload telemetry
  ERR,<message>
  READY
  # REII groundstation v...

Emits parsed events via a callback supplied by the caller.
"""

from __future__ import annotations

import re
import threading
import time
from typing import Callable, Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None  # type: ignore[assignment]

from protocol import UplinkFrame, MAGIC_UPLINK, PAYLOAD_SIZE


class SerialReader:
    """Blocking serial reader that parses groundstation lines and dispatches callbacks."""

    _ACK_PAYLOAD_RE = re.compile(r"^ACK,(\d),payload=([0-9A-Fa-f]{64})$")
    _PONG_RE = re.compile(r"^PONG,(\d),(\d+)$")
    _ECHO_RE = re.compile(r"^ECHO,(.*)$")

    def __init__(self, port: str, baud: int = 115200,
                 on_line: Optional[Callable[[dict], None]] = None) -> None:
        if serial is None:
            raise RuntimeError("pyserial is required: pip install pyserial")
        self.port = port
        self.baud = baud
        self.on_line = on_line
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._ser: Optional[serial.Serial] = None
        self._last_uplink: Optional[UplinkFrame] = None

    def start(self) -> bool:
        try:
            self._ser = serial.Serial(self.port, self.baud, timeout=0.2)
            time.sleep(0.5)
            self._ser.reset_input_buffer()
        except serial.SerialException as exc:
            print(f"[SerialReader] cannot open {self.port}: {exc}")
            return False
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        print(f"[SerialReader] opened {self.port} @ {self.baud}")
        return True

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()
        print("[SerialReader] stopped")

    def _run(self) -> None:
        assert self._ser is not None
        buf = bytearray()
        while not self._stop.is_set():
            try:
                data = self._ser.read(self._ser.in_waiting or 1)
                if not data:
                    continue
                buf.extend(data)
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip("\r").strip()
                    if text:
                        self._dispatch(text)
            except (serial.SerialException, OSError) as exc:
                print(f"[SerialReader] read error: {exc}")
                break

    def _dispatch(self, text: str) -> None:
        event: dict = {"raw": text, "ts": time.time()}

        m = self._ACK_PAYLOAD_RE.match(text)
        if m:
            ok = int(m.group(1))
            hex_payload = m.group(2)
            event["type"] = "ack_payload"
            event["ok"] = ok
            try:
                payload = bytes.fromhex(hex_payload)
                frame = UplinkFrame.unpack(payload)
                event["frame"] = {
                    "magic": frame.magic,
                    "seq_ack": frame.seq_ack,
                    "trolley_pos_m": frame.trolley_pos_m,
                    "hoist_pos_m": frame.hoist_pos_m,
                    "trolley_vel_m_s": frame.trolley_vel_m_s,
                    "hoist_vel_m_s": frame.hoist_vel_m_s,
                    "load_kg": frame.load_kg,
                    "vfd_current_A": frame.vfd_current_A,
                    "temp_c": frame.temp_c,
                    "magnet_status": frame.magnet_status,
                    "error_code": frame.error_code,
                    "fault_flags": frame.fault_flags,
                    "system_health": frame.system_health,
                }
                self._last_uplink = frame
            except ValueError as exc:
                event["parse_error"] = str(exc)
            if self.on_line:
                self.on_line(event)
            return

        m = self._PONG_RE.match(text)
        if m:
            event["type"] = "pong"
            event["ok"] = int(m.group(1))
            event["rtt_ms"] = int(m.group(2))
            if self.on_line:
                self.on_line(event)
            return

        m = self._ECHO_RE.match(text)
        if m:
            event["type"] = "echo"
            event["text"] = m.group(1)
            if self.on_line:
                self.on_line(event)
            return

        if text.startswith("OK,"):
            event["type"] = "ok"
        elif text.startswith("ERR,"):
            event["type"] = "error"
        elif text == "READY":
            event["type"] = "ready"
        elif text.startswith("#"):
            event["type"] = "banner"
        else:
            event["type"] = "unknown"

        if self.on_line:
            self.on_line(event)

    @property
    def last_uplink(self) -> Optional[UplinkFrame]:
        return self._last_uplink