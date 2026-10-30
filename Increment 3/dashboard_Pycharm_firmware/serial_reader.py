#!/usr/bin/env python3
"""
REII Dashboard Serial Reader — reads Groundstation USB-CDC (COM10 @ 115200).
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
    serial = None

from protocol import UplinkFrame


class SerialReader:
    _ACK_RE = re.compile(r"^ACK,(\d),payload=(.*)$")
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
            self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
            time.sleep(0.3)
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

        m = self._ACK_RE.match(text)
        if m:
            ok = int(m.group(1))
            payload_str = m.group(2).strip()
            if ok == 1 and len(payload_str) == 64 and payload_str != "none":
                event["type"] = "ack_payload"
                event["ok"] = 1
                try:
                    payload = bytes.fromhex(payload_str)
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
                        "magnet_current_A": frame.magnet_current_A,
                        "magnet_status": frame.magnet_status,
                        "error_code": frame.error_code,
                        "fault_flags": frame.fault_flags,
                        "system_health": frame.system_health,
                    }
                    self._last_uplink = frame
                except Exception as exc:
                    event["parse_error"] = str(exc)
            else:
                event["type"] = "ack_status"
                event["ok"] = ok
                event["has_payload"] = False

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
            event["type"] = "raw"

        if self.on_line:
            self.on_line(event)