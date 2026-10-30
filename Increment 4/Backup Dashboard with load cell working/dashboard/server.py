#!/usr/bin/env python3
"""REII crane dashboard - Python backend (run in a terminal) + web UI (browser).

    python server.py --port COM10                     # ground station only
    python server.py --port COM10 --rig-port COM12    # also show the crane's console
    then open http://localhost:5000

Needs only pyserial (python -m pip install -r requirements.txt). The web UI is
plain HTML/JS served from ./web, so it works offline in the lab.

HTTP API (all JSON)
  GET  /api/state     link + telemetry snapshot
  GET  /api/icd       Table 33 / 34 byte maps
  GET  /api/history   last 30 s of positions / load
  GET  /events        Server-Sent Events: a state snapshot every 100 ms + new log events
  POST /api/control   {"trolley": -100..100, "hoist": -100..100, "magnet_on", "hold_brake", "auto"}
                      (also the page's heartbeat for the deadman)
  POST /api/action    {"action": "soft_stop" | "release_stop" | "reset" | "zero" | "ping" | "pa" |
                       "load_tare" | "load_cal", "value": ...}
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import mimetypes
import pathlib
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import icd  # noqa: E402
from link import CraneLink  # noqa: E402

WEB = HERE / "web"
LINK: CraneLink | None = None
CONFIG: dict = {}


class Handler(BaseHTTPRequestHandler):
    server_version = "REIIDashboard/4.1"

    def log_message(self, fmt, *args):   # keep the terminal for link events
        pass

    # -------------------------------------------------------------- helpers
    def _json(self, obj, status: int = 200) -> None:
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _body(self) -> dict:
        n = int(self.headers.get("Content-Length") or 0)
        if not n:
            return {}
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except json.JSONDecodeError:
            return {}

    def _static(self, rel: str) -> None:
        path = (WEB / rel).resolve()
        if WEB not in path.parents and path != WEB or not path.is_file():
            self.send_error(404)
            return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")   # always the newest page, no hard refresh needed
        self.end_headers()
        self.wfile.write(data)

    # -------------------------------------------------------------- routes
    def do_GET(self):
        route = urlparse(self.path).path
        if route in ("/", "/index.html"):
            return self._static("index.html")
        if route.startswith("/static/"):
            return self._static(route[len("/static/"):])
        if route == "/api/state":
            return self._json(LINK.snapshot())
        if route == "/api/icd":
            return self._json({**icd.field_maps(), "config": CONFIG})
        if route == "/api/history":
            return self._json(LINK.history_since())
        if route == "/events":
            return self._sse()
        self.send_error(404)

    def do_POST(self):
        route = urlparse(self.path).path
        body = self._body()
        if route == "/api/control":
            LINK.set_controls(body)
            return self._json({"ok": True})
        if route == "/api/heartbeat":
            LINK.heartbeat()
            return self._json({"ok": True})
        if route == "/api/action":
            msg = LINK.action(str(body.get("action", "")), body.get("value"))
            return self._json({"ok": not msg.startswith("unknown"), "message": msg})
        self.send_error(404)

    def _sse(self) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        last_event = 0
        try:
            # replay the recent log so a freshly opened page has context
            for ev in LINK.events_after(0):
                self.wfile.write(f"event: log\ndata: {json.dumps(ev)}\n\n".encode())
                last_event = ev["id"]
            while True:
                snap = LINK.snapshot()
                self.wfile.write(f"event: state\ndata: {json.dumps(snap)}\n\n".encode())
                for ev in LINK.events_after(last_event):
                    self.wfile.write(f"event: log\ndata: {json.dumps(ev)}\n\n".encode())
                    last_event = ev["id"]
                self.wfile.flush()
                time.sleep(0.1)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            return


def main() -> int:
    global LINK, CONFIG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="ground station serial port, e.g. COM10")
    ap.add_argument("--rig-port", default=None, help="optional crane USB port (COM12) to show its console")
    ap.add_argument("--rate", type=float, default=20.0, help="command rate in Hz (ICD: 20-50, default 20)")
    ap.add_argument("--pa", default="LOW", choices=("MIN", "LOW", "HIGH", "MAX"),
                    help="radio power (bench < 2 m: LOW, rail 5-10 m: MAX)")
    ap.add_argument("--accel", type=float, default=100.0, help="S-curve ramp acceleration limit, %%/s")
    ap.add_argument("--jerk", type=float, default=400.0, help="S-curve ramp jerk limit, %%/s^2")
    ap.add_argument("--rail-mm", type=int, default=5000, help="rail length for the drawing (crane model: 5000)")
    ap.add_argument("--stroke-mm", type=int, default=1600, help="hoist stroke for the drawing (SysRS PT2: 1600)")
    ap.add_argument("--http-port", type=int, default=5000)
    ap.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to allow other PCs on the network")
    ap.add_argument("--open", action="store_true", help="open the dashboard in the default browser")
    args = ap.parse_args()

    if not 1 <= args.rate <= 50:
        ap.error("--rate must be 1..50 Hz")
    session = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_dir = HERE.parent / "results" / "dashboard" / session
    CONFIG = {"rail_mm": args.rail_mm, "stroke_mm": args.stroke_mm, "rate_hz": args.rate, "session": session}

    LINK = CraneLink(args.port, args.rig_port, args.rate, args.pa, args.accel, args.jerk, log_dir)
    LINK.start()

    httpd = ThreadingHTTPServer((args.host, args.http_port), Handler)
    httpd.daemon_threads = True
    url = f"http://localhost:{args.http_port}"
    print(f"REII crane dashboard\n  ground station : {args.port} @ {args.rate:g} Hz, PA {args.pa}"
          f"\n  crane console  : {args.rig_port or '(not used)'}\n  session log    : {log_dir}"
          f"\n  open           : {url}\n  stop           : Ctrl+C\n")
    if args.open:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping - the crane will enter safe hold when the commands stop")
    finally:
        LINK.stop()
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
