"""Ground-station link: USB serial (IF-E1.0) -> nRF24 (IF10.0) -> crane, and back.

One worker thread owns the ground-station port and runs the ICD operating sequence:

  1. USB check mode   ECHO round trip over the serial cable
  2. set up           APP (Normal Crane Control Mode), PROFILE ICD, PA <level>, CH <channel>
  3. radio check      INFO -> GS,LINK must show that channel / 2 Mbps / radio OK
  4. control loop     every 1/rate s: build a Table 33 frame from the operator's
                      state (through the S-curve ramp), send it with CMD <hex>,
                      read ACK,<ok>,payload=<Table 34 hex>,us=..,arc=..,seq=..

Safety behaviour (dashboard side only - the PHY2 hardware E-stop is separate):
  * deadman: if the web page stops sending its heartbeat for DEADMAN_S, the
    targets drop to 0 and the ramp brings the crane to rest
  * soft stop: latched until released; sets Table 33 b0 and zeroes speeds at once
  * the dashboard keeps transmitting while it runs; if it stops, the crane's own
    lost-link timer (rig_config.h RIG_LINK_TIMEOUT_MS) puts it in safe hold

Radio settings (power, channel) are changed on BOTH ends between two commands:
  * power: ground station over its serial port, crane over its USB console (--rig-port)
  * channel: only with --rig-port, because the crane can't yet be told over the air. The
    crane switches first, then the ground station; if no ACK comes back within
    CH_VERIFY_FAILS commands, both go back to the previous channel.
  * the dashboard is the reference: when the crane console is opened, or the crane
    reboots, the crane is set to the dashboard's channel and power.
"""

from __future__ import annotations

import csv
import datetime as dt
import pathlib
import queue
import re
import threading
import time
from collections import deque

import serial

import icd
import radio
from ramp import SCurveRamp

DEADMAN_S = 1.0
ACK_TIMEOUT_S = 0.15
LOST_AFTER_FAILS = 10          # consecutive missed ACKs -> link LOST (~0.5 s at 20 Hz)
WINDOW = 100                   # rolling window for PER / RTT
CH_VERIFY_FAILS = 6            # missed ACKs right after a channel change -> switch back (0.3 s at 20 Hz)
ACK_RX = re.compile(r"^ACK,(\d),payload=([0-9A-Fa-f]*)(?:,us=(\d+))?(?:,arc=(\d+))?(?:,seq=(\d+))?")


class SerialLines:
    """Background reader: every received line goes into a queue (and an optional log)."""

    def __init__(self, port: str, name: str, on_line=None):
        self.name = name
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.dtr = False   # no auto-reset / no download mode when the port opens
        self.ser.rts = False
        self.ser.open()
        self.q: queue.Queue[str] = queue.Queue(maxsize=2000)
        self.on_line = on_line
        self.alive = True
        self._buf = b""
        threading.Thread(target=self._run, daemon=True, name=f"{name}-reader").start()

    def _run(self) -> None:
        while self.alive:
            try:
                chunk = self.ser.read(512)
            except (serial.SerialException, OSError):
                self.alive = False
                break
            if not chunk:
                continue
            self._buf += chunk
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                line = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", raw.decode("utf-8", "replace")).strip()
                if not line:
                    continue
                if self.q.full():          # nobody is waiting on this port: keep only recent lines
                    try:
                        self.q.get_nowait()
                    except queue.Empty:
                        pass
                self.q.put_nowait(line)
                if self.on_line:
                    self.on_line(line)

    def send(self, text: str) -> None:
        self.ser.write((text + "\n").encode())

    def wait(self, pattern: str, timeout: float, others: list | None = None) -> str | None:
        rx = re.compile(pattern)
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return None
            try:
                line = self.q.get(timeout=left)
            except queue.Empty:
                return None
            if rx.search(line):
                return line
            if others is not None:
                others.append(line)

    def drain(self) -> list[str]:
        out = []
        while True:
            try:
                out.append(self.q.get_nowait())
            except queue.Empty:
                return out

    def close(self) -> None:
        self.alive = False
        try:
            self.ser.close()
        except Exception:
            pass


class CraneLink(threading.Thread):
    def __init__(self, gs_port: str, rig_port: str | None, rate_hz: float, pa: str,
                 accel: float, jerk: float, log_dir: pathlib.Path):
        super().__init__(daemon=True, name="crane-link")
        self.gs_port, self.rig_port = gs_port, rig_port
        self.period = 1.0 / rate_hz
        self.rate_hz = rate_hz
        self.pa = pa
        self.lock = threading.RLock()
        self.events: deque[dict] = deque(maxlen=300)
        self.event_id = 0

        # operator state (written by the web server)
        self.target_trolley = 0.0
        self.target_hoist = 0.0
        self.magnet_on = False
        self.hold_brake = False
        self.auto = False
        self.soft_stop = False
        self.pulse_reset = 0          # frames left to send the reset flag
        self.pulse_zero = 0
        self.ping_requested = False
        self.pending_pa: str | None = None
        self.pending_ch: int | None = None
        self.channel = radio.DEFAULT_CHANNEL
        self._ch_verify: dict | None = None        # {"old": ch, "fails": n} while a new channel is unproven
        self._revert_to: int | None = None
        self._rig_resync = False                   # set the crane to our channel / power at the next gap
        self.last_heartbeat = 0.0
        self.deadman_tripped = False

        self.ramp_trolley = SCurveRamp(accel, jerk)
        self.ramp_hoist = SCurveRamp(accel, jerk)

        # link state (read by the web server)
        self.state = "starting"       # starting | connecting | check_failed | running | lost | port_error
        self.state_detail = ""
        self.gs_link: dict = {}
        self.seq = 0
        self.sent = 0
        self.acked = 0
        self.consecutive_fail = 0
        self.window: deque[tuple[int, int | None, int]] = deque(maxlen=WINDOW)   # (ok, rtt_us, arc)
        self.last_down: bytes = b""
        self.last_up: bytes = b""
        self.last_cmd: dict = {}
        self.telemetry: dict | None = None
        self.telemetry_age_s: float | None = None
        self._tlm_time = 0.0
        self.rig_status: dict = {}
        self.rig_connected = False
        self._rig: SerialLines | None = None
        self.history: deque[dict] = deque(maxlen=600)   # 30 s at 20 Hz

        log_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir = log_dir
        self._csv_fh = (log_dir / "telemetry.csv").open("w", newline="", encoding="utf-8")
        self._csv = csv.writer(self._csv_fh)
        self._csv.writerow([
            "time", "seq", "ack", "rtt_us", "arc", "msg_type", "mode_flags", "trolley_cmd", "hoist_cmd",
            "magnet_state", "segment_idx", "tlm_seq_ack", "trolley_pos_mm", "hoist_pos_mm", "trolley_vel_mm_s",
            "hoist_vel_mm_s", "load_kg", "vfd_current_a", "magnet_current_a", "magnet_status", "error_code",
            "fault_flags", "system_health", "downlink_hex", "uplink_hex"])
        self._event_fh = (log_dir / "events.log").open("w", encoding="utf-8")
        self._last_faults = 0
        self._stop = threading.Event()

    # ------------------------------------------------------------------ events
    def event(self, level: str, text: str) -> None:
        with self.lock:
            self.event_id += 1
            ev = {"id": self.event_id, "t": dt.datetime.now().strftime("%H:%M:%S.%f")[:-3], "level": level,
                  "text": text}
            self.events.append(ev)
        self._event_fh.write(f"{ev['t']} [{level}] {text}\n")
        self._event_fh.flush()
        print(f"  [{level}] {text}")

    # ------------------------------------------------------------------ operator API (web thread)
    def set_controls(self, d: dict) -> None:
        with self.lock:
            self.last_heartbeat = time.monotonic()
            # While a soft stop is latched, speed requests are ignored, so releasing the
            # stop can never restart motion from a stale slider position.
            if "trolley" in d:
                self.target_trolley = 0.0 if self.soft_stop else max(-100.0, min(100.0, float(d["trolley"])))
            if "hoist" in d:
                self.target_hoist = 0.0 if self.soft_stop else max(-100.0, min(100.0, float(d["hoist"])))
            for key in ("magnet_on", "hold_brake", "auto"):
                if key in d:
                    setattr(self, key, bool(d[key]))
            if self.deadman_tripped:
                self.deadman_tripped = False
                self.event("info", "Dashboard page is back - controls re-enabled")

    def _warn(self, msg: str) -> str:
        self.event("warn", msg)
        return msg

    def heartbeat(self) -> None:
        with self.lock:
            self.last_heartbeat = time.monotonic()

    def action(self, name: str, value=None) -> str:
        with self.lock:
            if name == "soft_stop":
                self.soft_stop = True
                self.target_trolley = self.target_hoist = 0.0
                self.ramp_trolley.reset()
                self.ramp_hoist.reset()
                msg = "Soft stop sent - speeds zeroed (the red E-stop button is the real safety stop)"
            elif name == "release_stop":
                self.soft_stop = False
                msg = "Soft stop released"
            elif name == "reset":
                self.pulse_reset = 5
                msg = "Fault reset sent"
            elif name == "zero":
                self.pulse_zero = 5
                msg = "Zero position sent"
            elif name == "ping":
                self.ping_requested = True
                msg = "Ping queued"
            elif name in ("load_tare", "load_cal"):
                if not self._rig:
                    return self._warn("Tare / calibrate need the crane console: start the dashboard with --rig-port COM12")
                if name == "load_tare":
                    cmd, msg = "LOAD TARE", "Load cell tare sent to the crane - keep the hook unloaded for about 2 s"
                else:
                    try:
                        kg = float(value)
                    except (TypeError, ValueError):
                        return self._warn("Enter the known mass in kg first")
                    if not 0.05 <= kg <= 50.0:
                        return self._warn("Known mass must be between 0.05 and 50 kg")
                    cmd = f"LOAD CAL {kg:.3f}"
                    msg = f"Load cell calibration with {kg:.3f} kg sent - keep it hanging still for about 2 s"
                try:
                    self._rig.send(cmd)
                except (serial.SerialException, OSError) as exc:
                    return self._warn(f"Could not write to the crane console: {exc}")
            elif name == "pa" and str(value).upper() in radio.PA_DBM:
                self.pending_pa = str(value).upper()
                msg = f"Radio power change to {self.pending_pa} queued"
            elif name == "channel":
                try:
                    ch = int(value)
                except (TypeError, ValueError):
                    return self._warn("Pick a channel from the list")
                if ch not in radio.CLEAN_SET:
                    return self._warn(f"Channel {ch} is not one of the clean channels from the Increment 2 survey")
                if ch == self.channel:
                    return f"Already on channel {ch}"
                if not self._rig:
                    return self._warn("Changing channel needs the crane console: start the dashboard with "
                                      "--rig-port COM12 (the crane can't be told over the air yet)")
                if self.state != "running":
                    return self._warn("Changing channel needs a working link first")
                moving = (self.target_trolley or self.target_hoist or abs(self.ramp_trolley.v) > 0.5
                          or abs(self.ramp_hoist.v) > 0.5)
                if moving:
                    return self._warn("Bring the trolley and hoist to rest before changing channel")
                self.pending_ch = ch
                msg = f"Channel change {self.channel} -> {ch} ({2400 + ch} MHz) queued"
            else:
                return f"unknown action {name}"
        self.event("info", msg)
        return msg

    # ------------------------------------------------------------------ snapshot for the UI
    def snapshot(self) -> dict:
        with self.lock:
            win = list(self.window)
            oks = [w[0] for w in win]
            rtts = [w[1] / 1000 for w in win if w[0] and w[1] is not None]
            arcs = [w[2] for w in win if w[0]]
            tlm_age = time.monotonic() - self._tlm_time if self._tlm_time else None
            return {
                "state": self.state, "detail": self.state_detail, "gs_port": self.gs_port,
                "rig_port": self.rig_port, "rate_hz": self.rate_hz, "gs_link": self.gs_link,
                "stats": {
                    "sent": self.sent, "acked": self.acked,
                    "per_pct": round(100 * (1 - sum(oks) / len(oks)), 1) if oks else None,
                    "rtt_avg_ms": round(sum(rtts) / len(rtts), 2) if rtts else None,
                    "rtt_max_ms": round(max(rtts), 2) if rtts else None,
                    "retries_avg": round(sum(arcs) / len(arcs), 2) if arcs else None,
                    "consecutive_fail": self.consecutive_fail, "window": len(oks),
                },
                "operator": {
                    "target_trolley": self.target_trolley, "target_hoist": self.target_hoist,
                    "cmd_trolley": round(self.ramp_trolley.v, 1), "cmd_hoist": round(self.ramp_hoist.v, 1),
                    "magnet_on": self.magnet_on, "hold_brake": self.hold_brake, "auto": self.auto,
                    "soft_stop": self.soft_stop, "deadman_tripped": self.deadman_tripped,
                },
                "last_cmd": self.last_cmd, "telemetry": self.telemetry,
                "telemetry_age_s": round(tlm_age, 2) if tlm_age is not None else None,
                "downlink_hex": self.last_down.hex().upper(), "uplink_hex": self.last_up.hex().upper(),
                "rig": {"connected": self.rig_connected, **self.rig_status},
                "radio": {"channel": self.channel, "pa": self.pa, "verifying": self._ch_verify is not None,
                          "pending": self.pending_ch is not None or self.pending_pa is not None,
                          "can_change_channel": self._rig is not None},
                "log_dir": str(self.log_dir.relative_to(self.log_dir.parents[2])) if len(self.log_dir.parents) > 2 else str(self.log_dir),
            }

    def history_since(self, n: int = 600) -> list[dict]:
        with self.lock:
            return list(self.history)[-n:]

    def events_after(self, last_id: int) -> list[dict]:
        with self.lock:
            return [e for e in self.events if e["id"] > last_id]

    def stop(self) -> None:
        self._stop.set()
        # Leave the crane on the ICD channel, so the next session / smoke test finds it there.
        if self._rig and self.channel != radio.DEFAULT_CHANNEL:
            try:
                self._rig.send(f"CH {radio.DEFAULT_CHANNEL}")
                self._rig.wait(r"^OK,CH|^ERR", 1.0)
                print(f"crane set back to the ICD channel {radio.DEFAULT_CHANNEL}")
            except (serial.SerialException, OSError):
                pass

    # ------------------------------------------------------------------ crane console (optional)
    def _on_rig_line(self, line: str) -> None:
        if line.startswith(("RIG,HB,", "RIG,TLM,", "RIG,LOAD,")):
            kv = dict(p.split("=", 1) for p in line.split(",")[2:] if "=" in p)
            prefix = {"RIG,TLM": "tlm_", "RIG,LOA": "lc_"}.get(line[:7], "")
            with self.lock:
                self.rig_status.update({prefix + k: v for k, v in kv.items()})
                if prefix == "lc_":
                    self.rig_status["lc_time"] = time.time()
        elif line.startswith(("RIG,LOAD_TARE", "RIG,LOAD_CAL")):
            self.event("info", "Crane load cell: " + line[4:].replace("_", " ").replace(",done", " done").lower())
        elif line.startswith("RIG,BOOT,"):
            with self.lock:
                self._rig_resync = True
            self.event("warn", f"Crane rebooted - setting it to channel {self.channel}, power {self.pa}")
        elif line.startswith(("RIG,WARN", "RIG,ERR")):
            self.event("warn", "Crane: " + line.split(",", 2)[2])
        elif line.startswith("RIG,CMD,"):
            self.event("crane", "Crane applied " + line[len("RIG,CMD,"):])

    def _rig_load_stream(self) -> None:
        """Keeps the crane printing its own load reading at 5 Hz (LOAD WATCH), so the
        USB cross-check under Load updates live instead of every 5 s heartbeat."""
        while not self._stop.is_set() and self._rig:
            try:
                self._rig.send("LOAD WATCH 30")
            except (serial.SerialException, OSError):
                return
            self._stop.wait(25.0)

    # ------------------------------------------------------------------ main loop
    def run(self) -> None:
        rig = None
        if self.rig_port:
            try:
                rig = SerialLines(self.rig_port, "rig", self._on_rig_line)
                self._rig = rig
                self.rig_connected = True
                self._rig_resync = True
                self.event("info", f"Listening to the crane console on {self.rig_port}")
                threading.Thread(target=self._rig_load_stream, daemon=True, name="rig-load").start()
            except serial.SerialException as exc:
                self.event("warn", f"Crane console {self.rig_port} not available ({exc}) - continuing without it")
        while not self._stop.is_set():
            try:
                gs = SerialLines(self.gs_port, "gs")
            except serial.SerialException as exc:
                self._set_state("port_error", f"cannot open {self.gs_port}: {exc}")
                time.sleep(2.0)
                continue
            try:
                if self._connect(gs):
                    self._control_loop(gs)
            except (serial.SerialException, OSError) as exc:
                self._set_state("port_error", f"{self.gs_port} disconnected: {exc}")
            finally:
                gs.close()
            time.sleep(2.0)
        if rig:
            self._rig = None
            rig.close()
        self._csv_fh.close()
        self._event_fh.close()

    def _set_state(self, state: str, detail: str = "") -> None:
        changed = state != self.state
        with self.lock:
            self.state, self.state_detail = state, detail
        if changed:
            level = "info" if state in ("running", "connecting") else "warn"
            self.event(level, f"Link {state}" + (f": {detail}" if detail else ""))

    def _connect(self, gs: SerialLines) -> bool:
        self._set_state("connecting", f"checking ground station on {self.gs_port}")
        time.sleep(0.3)
        gs.drain()
        # 1. USB check mode
        token = f"dash{int(time.time()) % 100000}"
        gs.send(f"ECHO {token}")
        if not gs.wait(rf"^ECHO,{token}$", 3.0):
            gs.send("")   # a bare Enter makes the GS re-print its banner if it just booted
            if not gs.wait(r"^READY|^ECHO", 3.0):
                self._set_state("check_failed", "no ECHO reply - is the Increment 4 ground-station firmware flashed?")
                return False
        # 2. set up Normal Crane Control Mode, ICD profile, power, channel
        for cmd, pat in (("APP", r"^OK,app_ch="), ("PROFILE ICD", r"^OK,PROFILE"), (f"PA {self.pa}", r"^OK,PA"),
                         (f"CH {self.channel}", r"^OK,CH")):
            gs.send(cmd)
            reply = gs.wait(pat + r"|^ERR", 3.0)
            if not reply or reply.startswith("ERR"):
                self._set_state("check_failed", f"'{cmd}' -> {reply or 'no reply'}")
                return False
        # 3. radio check
        gs.send("INFO")
        link_line = gs.wait(r"^GS,LINK,", 3.0)
        gs.wait(r"^OK,INFO", 1.0)
        if not link_line:
            self._set_state("check_failed", "no GS,LINK line from INFO")
            return False
        d = dict(p.split("=", 1) for p in link_line.split(",")[2:] if "=" in p)
        with self.lock:
            self.gs_link = d
        if d.get("radio") != "OK" or d.get("ch") != str(self.channel) or d.get("rate") != "2M" or d.get("profile") != "ICD":
            self._set_state("check_failed", f"ground station not in ICD mode: {link_line}")
            return False
        self.event("info", f"Ground station ready: ch {d.get('ch')}, {d.get('rate')}bps, PA {d.get('pa')}, "
                           f"profile {d.get('profile')}")
        self._set_state("running")
        return True

    # ------------------------------------------------------------------ radio settings
    def _rig_cmd(self, cmd: str, ok_pattern: str) -> str | None:
        """Sends a console command to the crane over USB; returns its reply line (or None)."""
        rig = self._rig
        if not rig:
            return None
        try:
            rig.drain()
            rig.send(cmd)
        except (serial.SerialException, OSError) as exc:
            return f"ERR,{exc}"
        return rig.wait(ok_pattern + r"|^ERR", 1.0)

    @staticmethod
    def _rig_ok(reply: str | None) -> bool:
        return bool(reply) and reply.startswith("OK") and "cfg_errors=0" in reply

    def _note_crane(self, **kv) -> None:
        with self.lock:
            self.rig_status.update({k: str(v) for k, v in kv.items()})

    def _resync_crane(self) -> None:
        """Puts the crane on the dashboard's channel and power (after it boots or the console opens)."""
        if not self._rig:
            return
        r_ch = self._rig_cmd(f"CH {self.channel}", r"^OK,CH,")
        r_pa = self._rig_cmd(f"PA {self.pa}", r"^OK,PA,")
        if self._rig_ok(r_ch) and self._rig_ok(r_pa):
            self._note_crane(ch=self.channel, pa=self.pa)
            self.event("info", f"Crane set over USB to channel {self.channel}, power {self.pa}")
        else:
            self.event("warn", f"Could not set the crane over USB: CH -> {r_ch or 'no reply'}, PA -> {r_pa or 'no reply'}")

    def _set_power(self, gs: SerialLines, pa: str) -> None:
        r_crane = self._rig_cmd(f"PA {pa}", r"^OK,PA,") if self._rig else None
        gs.send(f"PA {pa}")
        r_gs = gs.wait(r"^OK,PA|^ERR", 2.0)
        gs_ok = bool(r_gs) and r_gs.startswith("OK")
        if gs_ok:
            self.pa = pa
            with self.lock:
                self.gs_link["pa"] = pa
        if not self._rig:
            crane = "unchanged (no --rig-port, so its replies stay at the old power)"
        elif self._rig_ok(r_crane):
            crane = "set over USB"
            self._note_crane(pa=pa)
        else:
            crane = f"NOT set ({r_crane or 'no reply'})"
        level = "info" if gs_ok and (not self._rig or self._rig_ok(r_crane)) else "warn"
        self.event(level, f"Radio power {pa} ({radio.PA_DBM[pa]} dBm): ground station "
                          f"{'set' if gs_ok else 'NOT set (' + (r_gs or 'no reply') + ')'}, crane {crane}")

    def _set_channel(self, gs: SerialLines, ch: int, revert: bool = False) -> None:
        old = self.channel
        r_crane = self._rig_cmd(f"CH {ch}", r"^OK,CH,")
        if not self._rig_ok(r_crane):
            if r_crane and r_crane.startswith("OK"):          # switched but registers didn't read back
                self._rig_cmd(f"CH {old}", r"^OK,CH,")
            self.event("warn", f"Channel {ch} not applied - the crane replied {r_crane or 'nothing'}; staying on {old}")
            return
        gs.send(f"CH {ch}")
        r_gs = gs.wait(r"^OK,CH|^ERR", 2.0)
        if not r_gs or not r_gs.startswith("OK"):
            self._rig_cmd(f"CH {old}", r"^OK,CH,")
            self.event("warn", f"Channel {ch} not applied - the ground station replied {r_gs or 'nothing'}; "
                               f"crane put back on {old}")
            return
        self.channel = ch
        with self.lock:
            self.gs_link["ch"] = str(ch)
        self._note_crane(ch=ch)
        if revert:
            self.event("warn", f"Back on channel {ch} ({2400 + ch} MHz). If the crane shows a wireless-timeout "
                               "fault, press Reset faults")
        else:
            self._ch_verify = {"old": old, "fails": 0}
            self.event("info", f"Channel {old} -> {ch} ({2400 + ch} MHz): crane switched over USB, ground station "
                               "switched - checking the link")

    def _check_new_channel(self, ok: int) -> None:
        v = self._ch_verify
        if ok:
            self._ch_verify = None
            self.event("info", f"Channel {self.channel} confirmed: the crane is answering")
            return
        v["fails"] += 1
        if v["fails"] >= CH_VERIFY_FAILS:
            self._ch_verify = None
            self.event("warn", f"No ACK on channel {self.channel} after {CH_VERIFY_FAILS} commands - "
                               f"switching both ends back to {v['old']}")
            with self.lock:
                self.pending_ch = None
            # applied between the next two commands, without another verification round
            self._revert_to = v["old"]

    def _control_loop(self, gs: SerialLines) -> None:
        next_t = time.monotonic()
        last_t = next_t
        while not self._stop.is_set():
            if not gs.alive:
                raise serial.SerialException("reader stopped")
            now = time.monotonic()
            dt_s = now - last_t
            last_t = now

            with self.lock:
                pa, self.pending_pa = self.pending_pa, None
                ch, self.pending_ch = self.pending_ch, None
                resync, self._rig_resync = self._rig_resync, False
                # deadman
                if self.last_heartbeat and now - self.last_heartbeat > DEADMAN_S and not self.deadman_tripped:
                    self.deadman_tripped = True
                    self.target_trolley = self.target_hoist = 0.0
                    trip = True
                else:
                    trip = False
                cmd = icd.Command(
                    msg_type=icd.MSG_PING if self.ping_requested else icd.MSG_MOVE,
                    sequence=self.seq, soft_stop=self.soft_stop, reset=self.pulse_reset > 0,
                    auto=self.auto, zero_pos=self.pulse_zero > 0, hold_brake=self.hold_brake,
                    magnet_on=self.magnet_on)
                self.ping_requested = False
                self.pulse_reset = max(0, self.pulse_reset - 1)
                self.pulse_zero = max(0, self.pulse_zero - 1)
                tgt_t = 0.0 if self.soft_stop else self.target_trolley
                tgt_h = 0.0 if self.soft_stop else self.target_hoist
                vt = self.ramp_trolley.step(tgt_t, dt_s)
                vh = self.ramp_hoist.step(tgt_h, dt_s)
                cmd.trolley_cmd = round(vt)
                cmd.hoist_cmd = round(vh)
                ramping = not (self.ramp_trolley.settled and self.ramp_hoist.settled)
                cmd.segment_idx = (self.ramp_trolley.steps + self.ramp_hoist.steps) & 0xFF if ramping else 0
            if trip:
                self.event("warn", f"Deadman: no heartbeat from the dashboard page for {DEADMAN_S:.0f} s - "
                                   "speeds ramped to 0")
            if resync:
                self._resync_crane()
            if pa:
                self._set_power(gs, pa)
            if ch is not None:
                self._set_channel(gs, ch)
            if self._revert_to is not None:
                back, self._revert_to = self._revert_to, None
                self._set_channel(gs, back, revert=True)

            frame = icd.pack_downlink(cmd)
            gs.send("CMD " + frame.hex().upper())
            others: list[str] = []
            reply = gs.wait(r"^ACK,|^ERR", ACK_TIMEOUT_S, others)
            for o in others:
                if o.startswith("ERR"):
                    self.event("warn", f"Ground station: {o}")
            self._handle_reply(cmd, frame, reply)

            self.seq = (self.seq + 1) & 0xFF
            next_t += self.period
            sleep = next_t - time.monotonic()
            if sleep > 0:
                time.sleep(sleep)
            else:
                next_t = time.monotonic()   # fell behind (slow serial) - don't try to catch up

    def _handle_reply(self, cmd: icd.Command, frame: bytes, reply: str | None) -> None:
        ok, rtt, arc, payload = 0, None, 0, b""
        m = ACK_RX.match(reply) if reply else None
        if m:
            ok = int(m.group(1))
            payload = bytes.fromhex(m.group(2)) if m.group(2) else b""
            rtt = int(m.group(3)) if m.group(3) else None
            arc = int(m.group(4)) if m.group(4) else 0
        tlm = None
        if ok and len(payload) == icd.FRAME_BYTES:
            try:
                tlm = icd.unpack_uplink(payload)
            except ValueError as exc:
                self.event("warn", f"Bad telemetry frame: {exc}")
        now = time.monotonic()
        with self.lock:
            self.sent += 1
            self.acked += ok
            self.window.append((ok, rtt, arc))
            self.last_down = frame
            self.last_cmd = icd.unpack_downlink(frame)
            if ok:
                self.consecutive_fail = 0
            else:
                self.consecutive_fail += 1
            if tlm:
                self.last_up = payload
                self.telemetry = tlm.to_dict()
                self._tlm_time = now
                self.history.append({"t": round(time.time(), 2), "trolley_pos": tlm.trolley_pos_mm,
                                     "hoist_pos": tlm.hoist_pos_mm, "load": tlm.load_kg,
                                     "cmd_trolley": cmd.trolley_cmd, "cmd_hoist": cmd.hoist_cmd})
            state = self.state
        if self._ch_verify is not None:
            self._check_new_channel(ok)
        # link state from consecutive failures
        if self.consecutive_fail >= LOST_AFTER_FAILS and state == "running":
            self._set_state("lost", f"{self.consecutive_fail} commands in a row without an ACK - check the crane")
        elif ok and state == "lost":
            self._set_state("running", "ACKs are coming back")
        if tlm and tlm.fault_flags != self._last_faults:
            new = tlm.fault_flags & ~self._last_faults
            cleared = self._last_faults & ~tlm.fault_flags
            for bit, name in icd.FAULT_FLAGS.items():
                if new >> bit & 1:
                    self.event("fault", f"Fault raised: {name} (error code {tlm.error_code})")
                if cleared >> bit & 1:
                    self.event("info", f"Fault cleared: {name}")
            self._last_faults = tlm.fault_flags
        t = tlm
        self._csv.writerow([
            dt.datetime.now().isoformat(timespec="milliseconds"), cmd.sequence, ok, rtt if rtt is not None else "",
            arc, cmd.msg_type, cmd.mode_flags, cmd.trolley_cmd, cmd.hoist_cmd, int(cmd.magnet_on), cmd.segment_idx,
            t.seq_ack if t else "", t.trolley_pos_mm if t else "", t.hoist_pos_mm if t else "",
            t.trolley_vel_mm_s if t else "", t.hoist_vel_mm_s if t else "", t.load_kg if t else "",
            t.vfd_current_a if t else "", t.magnet_current_a if t else "", t.magnet_status if t else "",
            t.error_code if t else "", t.fault_flags if t else "", t.system_health if t else "",
            frame.hex().upper(), payload.hex().upper()])
        if cmd.sequence % 20 == 0:
            self._csv_fh.flush()
