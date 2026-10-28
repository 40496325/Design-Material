#!/usr/bin/env python3
"""
REII Dashboard Backend — Flask + SocketIO (Architecture A1, decision D-10).

Reads groundstation USB-CDC serial (COM12 @ 115200), parses the 32-byte ICD
protocol (PROVISIONAL per D-01), and broadcasts telemetry + command status to
WebSocket clients. Accepts command frames from the browser and forwards them
to the groundstation as 32-byte downlink packets (hex via `CMD <hex>`).

Run:
    cd project_sources/03_increment_3/dashboard
    pip install -r requirements.txt
    python app.py
    # then open http://localhost:5000 in a browser
"""

from __future__ import annotations

import os
import sys
import time
import threading
from typing import Optional

from flask import Flask, render_template, jsonify, request
from flask_socketio import SocketIO, emit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from protocol import (  # noqa: E402
    DownlinkFrame, MAGIC_DOWNLINK, TYPE_CMD, PAYLOAD_SIZE, make_downlink,
)
from serial_reader import SerialReader  # noqa: E402

GS_PORT = os.environ.get("REII_GS_PORT", "COM10")
GS_BAUD = int(os.environ.get("REII_GS_BAUD", "115200"))
SECRET_KEY = os.environ.get("REII_SECRET_KEY", "reii-dev-secret")

app = Flask(__name__, template_folder="templates", static_folder="static")
app.config["SECRET_KEY"] = SECRET_KEY
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

serial_reader: Optional[SerialReader] = None
reader_lock = threading.Lock()

latest_telemetry: dict = {}
last_command: dict = {}
seq_counter = 0
seq_lock = threading.Lock()


def next_seq() -> int:
    global seq_counter
    with seq_lock:
        seq_counter = (seq_counter + 1) & 0xFF
        return seq_counter


def on_serial_event(event: dict) -> None:
    global latest_telemetry
    etype = event.get("type")

    if etype == "ack_payload":
        latest_telemetry = event.get("frame", {})
        latest_telemetry["ts"] = event.get("ts")
        socketio.emit("telemetry", latest_telemetry)
    elif etype == "pong":
        socketio.emit("pong", {"ok": event.get("ok"), "rtt_ms": event.get("rtt_ms")})
    elif etype == "echo":
        socketio.emit("echo", {"text": event.get("text")})
    elif etype == "ready":
        socketio.emit("status", {"status": "ready"})
    elif etype == "ok":
        socketio.emit("status", {"status": "ok", "detail": event.get("raw")})
    elif etype == "error":
        socketio.emit("status", {"status": "error", "detail": event.get("raw")})
    elif etype == "banner":
        socketio.emit("banner", {"text": event.get("raw")})
    else:
        socketio.emit("raw", {"text": event.get("raw")})


def start_serial_reader() -> bool:
    global serial_reader
    with reader_lock:
        if serial_reader is not None:
            return True
        serial_reader = SerialReader(port=GS_PORT, baud=GS_BAUD, on_line=on_serial_event)
        if not serial_reader.start():
            serial_reader = None
            return False
        print(f"[app] serial reader started on {GS_PORT}")
        return True


def stop_serial_reader() -> None:
    global serial_reader
    with reader_lock:
        if serial_reader is not None:
            serial_reader.stop()
            serial_reader = None


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/telemetry")
def api_telemetry():
    return jsonify(latest_telemetry if latest_telemetry else {"status": "no_data"})


@app.route("/api/status")
def api_status():
    return jsonify({
        "serial_open": serial_reader is not None,
        "port": GS_PORT,
        "baud": GS_BAUD,
    })


@socketio.on("connect")
def on_connect():
    print(f"[app] client connected: {request.sid}")
    emit("connected", {"status": "ok", "port": GS_PORT})
    if latest_telemetry:
        emit("telemetry", latest_telemetry)


@socketio.on("disconnect")
def on_disconnect():
    print(f"[app] client disconnected: {request.sid}")


@socketio.on("start_reader")
def on_start_reader():
    ok = start_serial_reader()
    emit("reader_started", {"ok": ok, "port": GS_PORT})


@socketio.on("stop_reader")
def on_stop_reader():
    stop_serial_reader()
    emit("reader_stopped", {"ok": True})


@socketio.on("command")
def on_command(data):
    """Receive a command from the browser and forward to groundstation via CMD hex."""
    global last_command
    try:
        estop = bool(data.get("estop", False))
        reset = bool(data.get("reset", False))
        manual = bool(data.get("manual", True))
        trolley = int(data.get("trolley", 0))
        hoist = int(data.get("hoist", 0))
        magnet = int(data.get("magnet", 0))

        trolley = max(-128, min(127, trolley))
        hoist = max(-128, min(127, hoist))
        magnet = max(0, min(255, magnet))

        frame = make_downlink(
            seq=next_seq(), estop=estop, reset=reset, manual=manual,
            trolley=trolley, hoist=hoist, magnet=magnet,
        )
        hex_frame = frame.pack().hex()

        last_command = {
            "seq": frame.seq,
            "estop": estop,
            "reset": reset,
            "manual": manual,
            "trolley": trolley,
            "hoist": hoist,
            "magnet": magnet,
            "hex": hex_frame,
            "ts": time.time(),
        }

        if serial_reader and serial_reader._ser and serial_reader._ser.is_open:
            cmd = f"CMD {hex_frame}\n"
            serial_reader._ser.write(cmd.encode("ascii"))
            serial_reader._ser.flush()
            emit("command_sent", {"ok": True, "seq": frame.seq, "hex": hex_frame})
        else:
            emit("command_sent", {"ok": False, "error": "serial not open"})

    except Exception as exc:
        emit("command_sent", {"ok": False, "error": str(exc)})


@socketio.on("get_telemetry")
def on_get_telemetry():
    emit("telemetry", latest_telemetry if latest_telemetry else {"status": "no_data"})


if __name__ == "__main__":
    start_serial_reader()
    try:
        socketio.run(app, host="0.0.0.0", port=5000, debug=False, use_reloader=False, allow_unsafe_werkzeug=True)
    finally:
        stop_serial_reader()