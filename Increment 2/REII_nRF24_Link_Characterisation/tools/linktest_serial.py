"""Thin serial helper shared by run_test.py and channel_analysis.py."""

from __future__ import annotations

import sys
import time

import serial


class LinkTestSerial:
    def __init__(self, port: str, baud: int = 115200, echo: bool = False):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.echo = echo
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def close(self) -> None:
        self.ser.close()

    def send(self, cmd: str) -> None:
        if self.echo:
            print(f">> {cmd}", file=sys.stderr)
        self.ser.write((cmd.strip() + "\n").encode())
        self.ser.flush()

    def readline(self) -> str | None:
        raw = self.ser.readline()
        if not raw:
            return None
        line = raw.decode(errors="replace").strip()
        if self.echo and line:
            print(f"<< {line}", file=sys.stderr)
        return line

    def wait_for(self, prefixes: tuple[str, ...], timeout_s: float) -> str | None:
        """Return the first line starting with any of `prefixes`, or None on timeout."""
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout_s:
            line = self.readline()
            if line and line.startswith(prefixes):
                return line
        return None

    def wait_ready(self, timeout_s: float = 10.0) -> bool:
        """Wait for the firmware READY banner (after a reset) or an answer to INFO."""
        line = self.wait_for(("READY",), 1.5)
        if line:
            return True
        self.send("CTRL")
        return self.wait_for(("OK",), timeout_s) is not None
