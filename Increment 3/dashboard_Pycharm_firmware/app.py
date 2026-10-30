#!/usr/bin/env python3
"""
REII Dashboard Backend — Flask + SocketIO with background telemetry poller.
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

from protocol import make_downlink
from serial_reader import SerialReader

GS_PORT = os.environ.get("REII_GS_PORT", "COM10")
GS_BAUD = int(os.environ.get("REII_GS_BAUD", "115200"))
SECRET_KEY = os.environ.get("REII_SECRET_KEY", "reii-dev-secret")

app = Flask(__name__, template_folder="templates", static_folder="static")
app.config["SECRET_KEY"] = SECRET_KEY
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

serial_reader: Optional[SerialReader] = None
reader_lock = threading.Lock()

latest_telemetry: dict = {}
current_ctrl: dict = {
    "estop": False,
    "reset": False,
    "manual": True,
    "trolley": 0,
    "hoist": 0,
    "magnet": 0,
}
ctrl_lock = threading.Lock()

seq_counter = 0
seq_lock = threading.Lock()
poll_thread: Optional[threading.Thread] = None
stop_poll = threading.Event()


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
        latest_telemetry["link_ok"] = True
        socketio.emit("telemetry", latest_telemetry)
    elif etype == "ack_status":
        ok = event.get("ok", 0)
        socketio.emit("link_status", {"link_ok": bool(ok)})
    elif etype == "pong":
        socketio.emit("pong", {"ok": event.get("ok"), "rtt_ms": event.get("rtt_ms")})
    elif etype == "ready":
        socketio.emit("status", {"status": "ready"})
    elif etype in ("ok", "error"):
        socketio.emit("status", {"status": etype, "detail": event.get("raw")})
    else:
        socketio.emit("raw", {"text": event.get("raw")})


def transmit_current_command():
    with ctrl_lock:
        frame = make_downlink(
            seq=next_seq(),
            estop=current_ctrl["estop"],
            reset=current_ctrl["reset"],
            manual=current_ctrl["manual"],
            trolley=current_ctrl["trolley"],
            hoist=current_ctrl["hoist"],
            magnet=current_ctrl["magnet"],
        )
        if current_ctrl["reset"]:
            current_ctrl["reset"] = False

    hex_frame = frame.pack().hex()
    if serial_reader and serial_reader._ser and serial_reader._ser.is_open:
        try:
            cmd = f"CMD {hex_frame}\n"
            serial_reader._ser.write(cmd.encode("ascii"))
            serial_reader._ser.flush()
        except Exception as exc:
            print(f"[app] serial write error: {exc}")


def telemetry_poll_loop():
    """Poll the crane at 5 Hz to continuously pull live telemetry via ACK payloads."""
    while not stop_poll.is_set():
        if serial_reader and serial_reader._ser and serial_reader._ser.is_open:
            transmit_current_command()
        time.sleep(0.2)


def start_serial_reader() -> bool:
    global serial_reader, poll_thread
    with reader_lock:
        if serial_reader is not None:
            return True
        serial_reader = SerialReader(port=GS_PORT, baud=GS_BAUD, on_line=on_serial_event)
        if not serial_reader.start():
            serial_reader = None
            return False

        stop_poll.clear()
        poll_thread = threading.Thread(target=telemetry_poll_loop, daemon=True)
        poll_thread.start()
        print(f"[app] serial reader and poller started on {GS_PORT}")
        return True


def stop_serial_reader() -> None:
    global serial_reader, poll_thread
    stop_poll.set()
    if poll_thread and poll_thread.is_alive():
        poll_thread.join(timeout=1.0)
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
    emit("connected", {"status": "ok", "port": GS_PORT})
    if latest_telemetry:
        emit("telemetry", latest_telemetry)


@socketio.on("command")
def on_command(data):
    with ctrl_lock:
        if "estop" in data:
            current_ctrl["estop"] = bool(data["estop"])
        if "reset" in data:
            current_ctrl["reset"] = bool(data["reset"])
        if "manual" in data:
            current_ctrl["manual"] = bool(data["manual"])
        if "trolley" in data:
            current_ctrl["trolley"] = max(-128, min(127, int(data["trolley"])))
        if "hoist" in data:
            current_ctrl["hoist"] = max(-128, min(127, int(data["hoist"])))
        if "magnet" in data:
            current_ctrl["magnet"] = 1 if data["magnet"] else 0

    transmit_current_command()
    emit("command_sent", {"ok": True})


if __name__ == "__main__":
    start_serial_reader()
    try:
        socketio.run(app, host="0.0.0.0", port=5000, debug=False, use_reloader=False, allow_unsafe_werkzeug=True)
    finally:
        stop_serial_reader()