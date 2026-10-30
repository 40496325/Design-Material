#!/usr/bin/env python3
"""REII Increment 4 - nRF24 link smoke test (ICD v1 IF10.0, progress record Milestone 4).

Talks to BOTH boards at once:
  * ground station  (NodeMCU-32S, PlatformIO firmware)  e.g. COM10
  * crane rig       (ESP32-S3, ESP-IDF firmware)         e.g. COM12

and produces evidence in results/i4_bench/<run_id>/:

  smoke.csv        one row per PING round (the main deliverable)
  sweep.csv        per-profile / per-PA summary rows
  diagnosis.json   RF diagnosis (only if a run got no link at all)
  groundstation.log, rig.log   full timestamped captures of both ports
  summary.json, SUMMARY.md     PASS/FAIL per gate
  smoke.png        RTT + cumulative delivery plot (if matplotlib is installed)

Gates
  G1  crane radio: SPI probe OK, every register read back, ch 77 / 2 Mbps, CE high
  G2  ground station radio: radio.begin() OK, APP mode ch 77 / 2 Mbps
  G3  both ends set to the same profile + PA
  G4  smoke: N PINGs, PER < 5.0 % and max RTT < 4.15 ms (ICD IF10.0 Table 32)
      ICD profile also checks the 0x81 telemetry in the ACK payload.

Usage (close every serial monitor first - Windows lets only one program own a port):
  python smoke_test.py --gs-port COM10 --rig-port COM12
  python smoke_test.py --gs-port COM10 --rig-port COM12 --profile static
  python smoke_test.py --gs-port COM10 --rig-port COM12 --sweep
  python smoke_test.py --gs-port COM10 --rig-port COM12 --diag-only
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import pathlib
import re
import statistics
import sys
import threading
import time
from dataclasses import dataclass, field

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    print("pyserial is missing:  python -m pip install -r requirements.txt", file=sys.stderr)
    sys.exit(2)

HERE = pathlib.Path(__file__).resolve().parent
RESULTS_ROOT = HERE.parent / "results" / "i4_bench"

PER_LIMIT_PCT = 5.0       # ICD / progress record smoke gate
RTT_LIMIT_MS = 4.15       # ICD IF10.0: worst-case delivery with ARC 5 / ARD 500 us
PA_NAMES = ("MIN", "LOW", "HIGH", "MAX")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

SMOKE_COLUMNS = [
    "run_id", "profile", "gs_pa", "rig_pa", "round", "seq", "ack", "rig_rx", "rtt_ms", "rtt_us", "arc",
    "tlm_len", "tlm_magic", "tlm_seq_ack", "tlm_health", "tlm_fault", "tlm_ok", "timestamp",
]
SWEEP_COLUMNS = [
    "run_id", "profile", "pa", "n", "acked", "per_pct", "rig_received", "ack_lost", "never_arrived",
    "rtt_min_ms", "rtt_avg_ms", "rtt_max_ms", "retries_total", "retries_max", "tlm_valid", "result",
]


# ---------------------------------------------------------------------------------------------
# Serial port with a background reader, a timestamped log and cursor-based waits
# ---------------------------------------------------------------------------------------------
@dataclass
class Line:
    t: float
    text: str


class Port:
    def __init__(self, name: str, device: str, log_path: pathlib.Path, echo: bool = False):
        self.name = name
        self.device = device
        self.echo = echo
        self.lines: list[Line] = []
        self.lock = threading.Lock()
        self.log = log_path.open("w", encoding="utf-8", newline="\n")
        self.ser = serial.Serial()
        self.ser.port = device
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        # DTR/RTS low before opening: no auto-reset / no download mode on either board.
        self.ser.dtr = False
        self.ser.rts = False
        try:
            self.ser.open()
        except serial.SerialException as exc:
            raise SystemExit(
                f"[{name}] cannot open {device}: {exc}\n"
                f"  -> is a serial monitor (VS Code / PlatformIO / idf.py monitor) still holding {device}?"
            )
        self._stop = False
        self._buf = b""
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self) -> None:
        while not self._stop:
            try:
                chunk = self.ser.read(512)
            except serial.SerialException as exc:
                self._record(f"!! serial error: {exc}")
                time.sleep(0.5)
                continue
            if not chunk:
                continue
            self._buf += chunk
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                text = ANSI.sub("", raw.decode("utf-8", errors="replace")).rstrip("\r")
                self._record(text)

    def _record(self, text: str) -> None:
        now = time.time()
        stamp = dt.datetime.fromtimestamp(now).strftime("%H:%M:%S.%f")[:-3]
        with self.lock:
            self.lines.append(Line(now, text))
            self.log.write(f"{stamp} {text}\n")
            self.log.flush()
        if self.echo:
            print(f"  [{self.name}] {text}")

    def mark(self) -> int:
        with self.lock:
            return len(self.lines)

    def since(self, idx: int) -> list[Line]:
        with self.lock:
            return list(self.lines[idx:])

    def send(self, cmd: str) -> int:
        """Writes one command line; returns a cursor positioned before any reply."""
        idx = self.mark()
        stamp = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        with self.lock:
            self.log.write(f"{stamp} >>> {cmd}\n")
        try:
            self.ser.write((cmd + "\n").encode())
            self.ser.flush()
        except serial.SerialException as exc:
            self._record(f"!! write failed: {exc}")
        return idx

    def wait(self, idx: int, pattern: str, timeout: float) -> Line | None:
        """First line at/after cursor `idx` matching regex `pattern` (search), or None."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        pos = idx
        while True:
            with self.lock:
                new = self.lines[pos:]
            for ln in new:
                pos += 1
                if rx.search(ln.text):
                    return ln
            if time.time() >= end:
                return None
            time.sleep(0.005)

    def command(self, cmd: str, ok_pattern: str, timeout: float = 3.0) -> tuple[Line | None, int]:
        idx = self.send(cmd)
        return self.wait(idx, ok_pattern + r"|^ERR", timeout), idx

    def pulse_reset(self) -> bool:
        """EN pulse through the USB-UART auto-reset circuit (RTS=1, DTR=0 -> EN low)."""
        try:
            self.ser.dtr = False
            self.ser.rts = True
            time.sleep(0.15)
            self.ser.rts = False
            return True
        except (OSError, serial.SerialException) as exc:
            self._record(f"!! reset via RTS not possible on this port ({exc}) - continuing without reset")
            return False

    def close(self) -> None:
        self._stop = True
        self.thread.join(timeout=1)
        try:
            self.ser.close()
        except Exception:
            pass
        self.log.close()


def kv(text: str) -> dict[str, str]:
    """'RIG,HB,a=1,b=x' -> {'a': '1', 'b': 'x'}"""
    out = {}
    for part in text.split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            out[k.strip()] = v.strip()
    return out


# ---------------------------------------------------------------------------------------------
# Gates
# ---------------------------------------------------------------------------------------------
@dataclass
class Gate:
    gid: str
    title: str
    passed: bool | None = None
    detail: list[str] = field(default_factory=list)

    def ok(self, msg: str) -> None:
        self.detail.append("OK   " + msg)

    def bad(self, msg: str) -> None:
        self.detail.append("FAIL " + msg)
        self.passed = False

    def note(self, msg: str) -> None:
        self.detail.append("     " + msg)

    def finish(self) -> bool:
        if self.passed is None:
            self.passed = True
        return self.passed


def gate_groundstation(gs: Port, reset: bool) -> Gate:
    g = Gate("G2", "Ground station radio (NodeMCU-32S)")
    idx = gs.mark()
    if reset:
        gs.pulse_reset()
        ready = gs.wait(idx, r"^READY", 8.0)
        if not ready:
            g.note("no READY after reset - asking with a bare Enter")
    begin = gs.wait(idx, r"radio\.begin\(\)", 0.5)
    if begin and "FAILED" in begin.text:
        g.bad("radio.begin() FAILED on the ground station - check its nRF wiring / 3V3")
    ln, i2 = gs.command("INFO", r"^OK,INFO", 4.0)
    if not ln or ln.text.startswith("ERR"):
        g.bad(f"ground station did not answer INFO on {gs.device} (wrong port, or firmware not flashed?)")
        return g
    link = gs.wait(i2, r"^GS,LINK", 0.5)
    if not link:
        g.bad("no GS,LINK line - is the Increment 4 ground-station firmware flashed?")
        return g
    d = kv(link.text)
    if d.get("radio") != "OK":
        g.bad("ground station reports radio=NOT_RESPONDING")
    else:
        g.ok("radio.begin() OK")
    if begin and "isPVariant=1" in begin.text:
        g.ok("isPVariant=1 (nRF24L01+)")
    lines = " ".join(x.text for x in gs.since(i2))
    for want, label in (("Channel", "channel"), ("RF Data Rate", "data rate"), ("CRC Length", "CRC")):
        if want not in lines:
            g.note(f"register dump missing '{label}'")
    g.note(link.text)
    return g


def gate_rig(rig: Port) -> tuple[Gate, bool]:
    """Returns (gate, console_ok). console_ok = the crane answers typed commands."""
    g = Gate("G1", "Crane radio (ESP32-S3, J1 + CE)")
    ln = None
    i2 = 0
    for attempt in range(3):
        ln, i2 = rig.command("INFO", r"^OK,INFO", 4.0)
        if ln:
            break
        time.sleep(1.5)
    if not ln:
        g.bad(f"crane did not answer INFO on {rig.device} - is the Increment 4 crane firmware flashed, "
              f"and is {rig.device} the board's USB-C port?")
        return g, False
    if ln.text.startswith("ERR"):
        g.bad(ln.text)
        return g, True
    info = rig.wait(i2, r"^RIG,INFO", 0.5)
    link = rig.wait(i2, r"^RIG,LINK", 0.5)
    regs = rig.wait(i2, r"^RIG,REGS", 0.5)
    addr = rig.wait(i2, r"^RIG,ADDR", 0.5)
    if info:
        d = kv(info.text)
        if d.get("probe") == "OK":
            g.ok(f"SPI probe OK ({d.get('variant')}), SETUP_AW={d.get('setup_aw')}")
        else:
            g.bad(f"SPI probe failed: {d.get('probe')} SETUP_AW={d.get('setup_aw')} - check J1 wiring / +3.3VP")
    errs = rig.since(i2)
    for e in errs:
        if e.text.startswith("RIG,ERR,read-back"):
            g.bad(e.text[len("RIG,ERR,"):])
    if link:
        d = kv(link.text)
        if d.get("cfg_errors") not in ("0", None):
            g.bad(f"{d.get('cfg_errors')} register(s) did not read back")
        ce_vals = [d.get("ce_j7_10"), d.get("ce_j3_3")]
        paths = d.get("ce_paths") or ""
        if paths.startswith("J3.3") and "J7.10" not in paths and d.get("ce_j3_3") != "1":
            g.bad(f"CE is set to J3.3 only, but J3.3 reads {d.get('ce_j3_3')} ({paths})")
        elif "1" in ce_vals:
            g.ok(f"CE high (J7.10={d.get('ce_j7_10')}, J3.3={d.get('ce_j3_3')}, paths {d.get('ce_paths')})")
        else:
            g.bad(f"CE not high on any path (J7.10={d.get('ce_j7_10')}, J3.3={d.get('ce_j3_3')})")
        if "-1" == d.get("ce_j3_3") and "MCP" in (d.get("ce_paths") or ""):
            g.note("MCP23017 read-back unavailable - J3.3 CE cannot be confirmed")
        g.note(link.text)
    if regs:
        d = kv(regs.text)
        g.note(regs.text)
        if d.get("CONFIG", "").upper() not in ("0F",):
            g.bad(f"CONFIG={d.get('CONFIG')} (expected 0F = CRC16, powered, PRX)")
    if addr:
        g.note(addr.text)
        if "('REII2')" in addr.text:
            g.ok("listening on 'REII2'")
    return g, True


def configure(gs: Port, rig: Port | None, profile: str, pa: str, rig_console: bool) -> Gate:
    g = Gate("G3", f"Both ends set to profile {profile}, APP mode, PA {pa}")
    steps_gs = [("APP", r"^OK,app_ch=77"), (f"PROFILE {profile}", r"^OK,PROFILE"), (f"PA {pa}", r"^OK,PA"),
                ("CLEAR", r"^OK,CLEAR")]
    for cmd, pat in steps_gs:
        ln, _ = gs.command(cmd, pat, 3.0)
        if not ln or ln.text.startswith("ERR"):
            g.bad(f"GS '{cmd}' -> {ln.text if ln else 'no reply'}")
    if rig and rig_console:
        steps_rig = [("MODE APP", r"^OK,MODE"), (f"PROFILE {profile}", r"^OK,PROFILE"), (f"PA {pa}", r"^OK,PA"),
                     ("LOG 1", r"^OK,LOG"), ("CLEAR", r"^OK,CLEAR")]
        for cmd, pat in steps_rig:
            ln, _ = rig.command(cmd, pat, 3.0)
            if not ln or ln.text.startswith("ERR"):
                g.bad(f"crane '{cmd}' -> {ln.text if ln else 'no reply'}")
            elif "cfg_errors=" in ln.text and not ln.text.endswith("cfg_errors=0"):
                g.bad(f"crane '{cmd}' -> {ln.text}")
    else:
        g.note("crane console not available - crane must already be in the same profile (its boot default is ICD)")
    _, i = gs.command("INFO", r"^OK,INFO", 3.0)
    link = gs.wait(i, r"^GS,LINK", 0.5)
    if link:
        d = kv(link.text)
        if d.get("ch") != "77" or d.get("rate") != "2M" or d.get("profile") != profile or d.get("pa") != pa:
            g.bad(f"GS link line does not match: {link.text}")
        else:
            g.ok(f"GS {link.text[len('GS,LINK,'):]}")
    if rig and rig_console:
        _, i = rig.command("STATS", r"^OK,STATS", 3.0)
        hb = rig.wait(i, r"^RIG,STATS", 0.5)
        if hb:
            d = kv(hb.text)
            if d.get("ch") != "77" or d.get("profile") != profile or d.get("pa") != pa:
                g.bad(f"crane does not match: {hb.text}")
            else:
                g.ok(f"crane profile={d.get('profile')} ch={d.get('ch')} pa={d.get('pa')} config={d.get('config')}")
    return g


# ---------------------------------------------------------------------------------------------
# Smoke run
# ---------------------------------------------------------------------------------------------
PONG_RX = re.compile(r"^PONG,(\d),(\d+),(\d+),(\d+),(\d+)")
TLM_RX = re.compile(r"^TLM,(\d+),([0-9A-Fa-f]*)")
RIGRX_RX = re.compile(r"^RIG,RX,(\d+),(\w+),(\w+),(\d+),(\d+),(\d)")


def smoke(gs: Port, rig: Port | None, rig_console: bool, run_id: str, profile: str, pa: str, n: int,
          interval_ms: int, quiet: bool) -> tuple[list[dict], dict]:
    rows: list[dict] = []
    rig_idx = rig.mark() if rig else 0
    t_start = time.time()
    for rnd in range(1, n + 1):
        seq = (rnd - 1) % 256
        idx = gs.send(f"PING {seq}")
        t_send = time.time()
        pong = gs.wait(idx, r"^PONG,|^ERR", 1.5)
        row = {
            "run_id": run_id, "profile": profile, "gs_pa": pa, "rig_pa": pa if rig_console else "",
            "round": rnd, "seq": seq, "ack": 0, "rig_rx": "", "rtt_ms": "", "rtt_us": "", "arc": "",
            "tlm_len": "", "tlm_magic": "", "tlm_seq_ack": "", "tlm_health": "", "tlm_fault": "", "tlm_ok": "",
            "timestamp": dt.datetime.fromtimestamp(t_send).isoformat(timespec="milliseconds"),
            "_t": t_send,
        }
        m = PONG_RX.match(pong.text) if pong else None
        if m:
            ok, _ms, arc, us, _seq = (int(x) for x in m.groups())
            row["ack"] = ok
            row["arc"] = arc
            if ok:
                row["rtt_us"] = us
                row["rtt_ms"] = f"{us / 1000:.3f}"
            if profile == "ICD":
                tl = gs.wait(idx, r"^TLM,", 0.3)
                tm = TLM_RX.match(tl.text) if tl else None
                if tm:
                    ln = int(tm.group(1))
                    payload = bytes.fromhex(tm.group(2)) if ln else b""
                    row["tlm_len"] = ln
                    if ln >= 19:
                        row["tlm_magic"] = f"0x{payload[0]:02X}"
                        row["tlm_seq_ack"] = payload[1]
                        row["tlm_fault"] = f"0x{payload[17]:02X}"
                        row["tlm_health"] = f"0x{payload[18]:02X}"
                        row["tlm_ok"] = int(ln == 32 and payload[0] == 0x81)
                    elif ok:
                        row["tlm_ok"] = 0
        elif pong and pong.text.startswith("ERR"):
            print(f"  ground station refused PING: {pong.text}", file=sys.stderr)
        rows.append(row)
        if not quiet and (rnd % 25 == 0 or rnd == n):
            acked = sum(r["ack"] for r in rows)
            print(f"    {profile:6s} PA {pa:4s} round {rnd:4d}/{n}: acked {acked}/{rnd}")
        sleep_left = interval_ms / 1000 - (time.time() - t_send)
        if sleep_left > 0:
            time.sleep(sleep_left)
    time.sleep(0.4)   # let the last RIG,RX lines arrive

    # Correlate with what the crane logged (RIG,RX,<n>,<type>,<seq>,...), in time order.
    if rig and rig_console:
        rx_events = []
        for ln in rig.since(rig_idx):
            m = RIGRX_RX.match(ln.text)
            if m and m.group(2) == "1":   # type 1 = PING
                rx_events.append((ln.t, int(m.group(3))))
        used = [False] * len(rx_events)
        start = 0
        for row in rows:
            row["rig_rx"] = 0
            for j in range(start, len(rx_events)):
                if used[j]:
                    continue
                t_rx, s_rx = rx_events[j]
                if t_rx < row["_t"] - 0.25:
                    continue
                if t_rx > row["_t"] + 1.5:
                    break
                if s_rx == row["seq"]:
                    used[j] = True
                    row["rig_rx"] = 1
                    start = j + 1
                    break
    stats = summarise(rows, profile, pa, run_id, rig_console)
    stats["duration_s"] = round(time.time() - t_start, 1)
    return rows, stats


def summarise(rows: list[dict], profile: str, pa: str, run_id: str, rig_console: bool) -> dict:
    n = len(rows)
    acked = sum(r["ack"] for r in rows)
    rtts = [r["rtt_us"] / 1000 for r in rows if r["ack"] and r["rtt_us"] != ""]
    arcs = [r["arc"] for r in rows if r["arc"] != ""]
    rig_rx = sum(1 for r in rows if r["rig_rx"] == 1) if rig_console else None
    ack_lost = sum(1 for r in rows if r["rig_rx"] == 1 and not r["ack"]) if rig_console else None
    never = sum(1 for r in rows if r["rig_rx"] == 0 and not r["ack"]) if rig_console else None
    tlm_valid = sum(1 for r in rows if r.get("tlm_ok") == 1)
    per = 100.0 * (1 - acked / n) if n else 100.0
    rtt_max = max(rtts) if rtts else None
    passed = n > 0 and per < PER_LIMIT_PCT and rtt_max is not None and rtt_max < RTT_LIMIT_MS
    tlm_note = None
    if profile == "ICD" and acked:
        lag_ok = sum(1 for i, r in enumerate(rows) if r.get("tlm_ok") == 1 and i > 0
                     and r["tlm_seq_ack"] == rows[i - 1]["seq"])
        tlm_note = (f"{tlm_valid}/{acked} ACKs carried a valid 0x81 telemetry frame; "
                    f"{lag_ok} of them echo the previous round's seq (the ACK payload is pre-loaded, so it "
                    f"answers the command before this one)")
    return {
        "run_id": run_id, "profile": profile, "pa": pa, "n": n, "acked": acked, "per_pct": round(per, 2),
        "rig_received": rig_rx, "ack_lost": ack_lost, "never_arrived": never,
        "rtt_min_ms": round(min(rtts), 3) if rtts else None,
        "rtt_avg_ms": round(statistics.mean(rtts), 3) if rtts else None,
        "rtt_max_ms": round(rtt_max, 3) if rtt_max is not None else None,
        "retries_total": sum(arcs) if arcs else 0, "retries_max": max(arcs) if arcs else 0,
        "tlm_valid": tlm_valid if profile == "ICD" else None, "tlm_note": tlm_note,
        "result": "PASS" if passed else "FAIL",
    }


# ---------------------------------------------------------------------------------------------
# RF diagnosis: which radio is deaf / mute?
# ---------------------------------------------------------------------------------------------
def diagnose(gs: Port, rig: Port, rig_console: bool) -> dict:
    print("  running RF diagnosis (receiver scans + carrier/RPD both ways) ...")
    out: dict = {"findings": []}
    ln, i = gs.command("SCAN 20", r"^OK,SCAN", 30.0)
    end = gs.wait(i, r"^SCAN_END", 0.5)
    out["gs_scan_total"] = int(kv(end.text).get("total_hits", "-1")) if end else None
    if rig_console:
        ln, i = rig.command("SCAN 20", r"^OK,SCAN", 30.0)
        end = rig.wait(i, r"^RIG,SCAN_END", 0.5)
        out["rig_scan_total"] = int(end.text.split(",")[2]) if end else None

        # crane -> GS: crane carrier, GS measures RPD
        i = rig.send("CARRIER 3000")
        on = rig.wait(i, r"^RIG,CARRIER,ON", 3.0)
        if on:
            time.sleep(0.2)
            _, j = gs.command("RPD 2000", r"^OK,RPD", 6.0)
            r = gs.wait(j, r"^RPD,", 0.5)
            if r:
                h, s = (int(x) for x in r.text.split(",")[1:3])
                out["crane_to_gs_rpd"] = [h, s]
        rig.wait(i, r"^OK,CARRIER", 6.0)

        # GS -> crane: GS carrier, crane measures RPD
        i = gs.send("CARRIER 3000")
        on = gs.wait(i, r"^GS,CARRIER,ON", 3.0)
        if on:
            time.sleep(0.2)
            _, j = rig.command("RPD 2000", r"^OK,RPD", 8.0)
            r = rig.wait(j, r"^RIG,RPD,", 0.5)
            if r:
                h, s = (int(x) for x in r.text.split(",")[2:4])
                out["gs_to_crane_rpd"] = [h, s]
        gs.wait(i, r"^OK,CARRIER", 6.0)

    f = out["findings"]
    gs_scan, rig_scan = out.get("gs_scan_total"), out.get("rig_scan_total")
    c2g, g2c = out.get("crane_to_gs_rpd"), out.get("gs_to_crane_rpd")
    if gs_scan == 0:
        f.append("Ground-station receiver heard NO energy on any channel (Wi-Fi should light up ch ~2-82): "
                 "its nRF receive path is dead (module, antenna, decoupling).")
    if rig_scan == 0:
        f.append("Crane receiver heard NO energy on any channel: crane nRF receive path is dead, or CE is not "
                 "reaching the module (check the CE jumper on J3.3 / J7.10).")
    if c2g and c2g[1] and c2g[0] / c2g[1] < 0.5:
        f.append(f"GS did not hear the crane's carrier (RPD {c2g[0]}/{c2g[1]}): crane transmitter or CE path at "
                 f"fault, or the radios are too far apart.")
    elif c2g:
        f.append(f"Crane -> GS carrier heard (RPD {c2g[0]}/{c2g[1]}): crane TX + CE work.")
    if g2c and g2c[1] and g2c[0] / g2c[1] < 0.5:
        f.append(f"Crane did not hear the GS carrier (RPD {g2c[0]}/{g2c[1]}): crane receiver / CE at fault, or "
                 f"GS transmitter dead.")
    elif g2c:
        f.append(f"GS -> crane carrier heard (RPD {g2c[0]}/{g2c[1]}): GS TX and crane RX work.")
    if c2g and g2c and c2g[1] and g2c[1] and c2g[0] / c2g[1] >= 0.5 and g2c[0] / g2c[1] >= 0.5:
        f.append("Both radios transmit and receive, yet no packets: the two ends disagree on a link setting "
                 "(profile ICD vs STATIC, channel, data rate, address, CRC). Compare the GS,LINK and RIG,LINK "
                 "lines in the logs.")
    if not rig_console:
        f.append("Crane console not available, so only the ground-station scan could run.")
    for x in f:
        print("   - " + x)
    return out


# ---------------------------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------------------------
def write_csv(path: pathlib.Path, columns: list[str], rows: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=columns, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: ("" if r.get(k) is None else r.get(k)) for k in columns})


def plot(path: pathlib.Path, rows: list[dict]) -> bool:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return False
    groups: dict[str, list[dict]] = {}
    for r in rows:
        groups.setdefault(f"{r['profile']} PA {r['gs_pa']}", []).append(r)
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=False)
    for label, rs in groups.items():
        xs = [r["round"] for r in rs if r["ack"]]
        ys = [r["rtt_us"] / 1000 for r in rs if r["ack"]]
        axes[0].plot(xs, ys, ".", ms=4, label=label)
        cum_ack, cum_rx, a, b = [], [], 0, 0
        for r in rs:
            a += r["ack"]
            b += 1 if r["rig_rx"] == 1 else 0
            cum_ack.append(a)
            cum_rx.append(b)
        rounds = [r["round"] for r in rs]
        axes[1].plot(rounds, cum_ack, label=f"{label}: GS ACKed")
        if any(r["rig_rx"] != "" for r in rs):
            axes[1].plot(rounds, cum_rx, "--", label=f"{label}: crane received")
    axes[0].axhline(RTT_LIMIT_MS, color="red", lw=1, ls=":", label=f"{RTT_LIMIT_MS} ms limit")
    axes[0].set_ylabel("RTT [ms]")
    axes[0].set_title("REII Increment 4 smoke test - ch 77 / 2 Mbps")
    axes[0].legend(fontsize=8)
    axes[1].set_xlabel("round")
    axes[1].set_ylabel("cumulative packets")
    axes[1].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return True


def write_summary_md(path: pathlib.Path, run_id: str, args: argparse.Namespace, gates: list[Gate],
                     runs: list[dict], diag: dict | None) -> None:
    lines = [f"# REII Increment 4 smoke test - run {run_id}", "",
             f"* Ground station: `{args.gs_port}`  |  Crane: `{args.rig_port}`",
             "* Link: ch 77 (2477 MHz), 2 Mbps, CRC-16, 5-byte addresses, ARC 5 / ARD 500 us "
             "(ICD v1 IF10.0)",
             f"* Pass criteria: PER < {PER_LIMIT_PCT} % and max RTT < {RTT_LIMIT_MS} ms", "", "## Gates", ""]
    for g in gates:
        lines.append(f"### {g.gid} {g.title}: **{'PASS' if g.passed else 'FAIL'}**")
        lines.append("")
        lines += [f"    {d}" for d in g.detail]
        lines.append("")
    if runs:
        lines += ["## Smoke runs", "",
                  "| profile | PA | n | ACKed | PER % | crane rx | ACK lost | never arrived | RTT avg / max ms | "
                  "retries (max) | telemetry | result |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in runs:
            lines.append(
                f"| {r['profile']} | {r['pa']} | {r['n']} | {r['acked']} | {r['per_pct']} | "
                f"{'' if r['rig_received'] is None else r['rig_received']} | "
                f"{'' if r['ack_lost'] is None else r['ack_lost']} | "
                f"{'' if r['never_arrived'] is None else r['never_arrived']} | "
                f"{r['rtt_avg_ms']} / {r['rtt_max_ms']} | {r['retries_total']} ({r['retries_max']}) | "
                f"{'' if r['tlm_valid'] is None else r['tlm_valid']} | **{r['result']}** |")
        lines.append("")
        for r in runs:
            if r.get("tlm_note"):
                lines.append(f"* {r['profile']} PA {r['pa']}: {r['tlm_note']}")
        lines.append("")
    if diag:
        lines += ["## RF diagnosis", "", "```", json.dumps({k: v for k, v in diag.items() if k != 'findings'},
                                                          indent=2), "```", ""]
        lines += [f"* {x}" for x in diag.get("findings", [])]
        lines.append("")
    lines += ["## Files", "", "* `smoke.csv` - one row per round (rig_rx = the crane logged that seq, so "
              "'never arrived' and 'arrived but ACK lost' are separated)",
              "* `sweep.csv` - one row per profile / PA run", "* `groundstation.log`, `rig.log` - raw captures",
              ""]
    path.write_text("\n".join(lines), encoding="utf-8")


# ---------------------------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gs-port", required=True, help="ground station serial port (e.g. COM10)")
    ap.add_argument("--rig-port", default=None, help="crane USB port (e.g. COM12); omit to test GS-only")
    ap.add_argument("--profile", choices=("icd", "static", "both"), default="icd",
                    help="payload profile(s) to test (default: icd = Normal Crane Control Mode; "
                         "'both' adds the STATIC bring-up profile first)")
    ap.add_argument("--pa", choices=PA_NAMES, default="LOW", help="PA level on both ends (bench < 2 m: LOW)")
    ap.add_argument("--sweep", action="store_true", help="repeat the smoke run at MIN/LOW/HIGH/MAX")
    ap.add_argument("--n", type=int, default=200, help="PINGs per run (default 200)")
    ap.add_argument("--interval-ms", type=int, default=50, help="PING period in ms (default 50 = 20 Hz)")
    ap.add_argument("--diag-only", action="store_true", help="only run the RF diagnosis")
    ap.add_argument("--no-diag", action="store_true", help="never run the RF diagnosis")
    ap.add_argument("--no-reset", action="store_true", help="do not reset the ground station at start")
    ap.add_argument("--out", default=None, help="results folder (default ../results/i4_bench)")
    ap.add_argument("--echo", action="store_true", help="print every serial line as it arrives")
    args = ap.parse_args()

    run_id = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = (pathlib.Path(args.out) if args.out else RESULTS_ROOT) / run_id
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"REII Increment 4 smoke test  run {run_id}\n  results -> {out_dir}")

    gs = Port("GS", args.gs_port, out_dir / "groundstation.log", args.echo)
    rig = Port("RIG", args.rig_port, out_dir / "rig.log", args.echo) if args.rig_port else None
    gates: list[Gate] = []
    runs: list[dict] = []
    all_rows: list[dict] = []
    diag = None
    try:
        g2 = gate_groundstation(gs, reset=not args.no_reset)
        rig_console = False
        if rig:
            g1, rig_console = gate_rig(rig)
            gates.append(g1)
            g1.finish()
        gates.append(g2)
        g2.finish()
        for g in gates:
            print(f"  {g.gid} {g.title}: {'PASS' if g.passed else 'FAIL'}")
            for d in g.detail:
                if d.startswith("FAIL"):
                    print(f"     {d}")

        if not g2.passed:
            print("  ground station not usable - stopping (see groundstation.log)")
        elif args.diag_only:
            if rig:
                diag = diagnose(gs, rig, rig_console)
        else:
            profiles = {"icd": ["ICD"], "static": ["STATIC"], "both": ["STATIC", "ICD"]}[args.profile]
            pas = list(PA_NAMES) if args.sweep else [args.pa]
            for profile in profiles:
                for pa in pas:
                    g3 = configure(gs, rig, profile, pa, rig_console)
                    g3.finish()
                    gates.append(g3)
                    print(f"  {g3.gid} {g3.title}: {'PASS' if g3.passed else 'FAIL'}")
                    for d in g3.detail:
                        if d.startswith("FAIL"):
                            print(f"     {d}")
                    print(f"  G4 smoke {profile} PA {pa}: {args.n} PINGs @ {args.interval_ms} ms ...")
                    rows, st = smoke(gs, rig, rig_console, run_id, profile, pa, args.n, args.interval_ms, False)
                    all_rows += rows
                    runs.append(st)
                    g4 = Gate("G4", f"Smoke {profile} PA {pa}")
                    g4.passed = st["result"] == "PASS"
                    g4.note(f"PER {st['per_pct']} % (limit {PER_LIMIT_PCT}), RTT max {st['rtt_max_ms']} ms "
                            f"(limit {RTT_LIMIT_MS}), crane received {st['rig_received']}, "
                            f"ACK lost {st['ack_lost']}, never arrived {st['never_arrived']}")
                    if st.get("tlm_note"):
                        g4.note(st["tlm_note"])
                    gates.append(g4)
                    print(f"     -> {st['result']}: PER {st['per_pct']} %, RTT avg {st['rtt_avg_ms']} / max "
                          f"{st['rtt_max_ms']} ms, crane rx {st['rig_received']}, ACK lost {st['ack_lost']}")
                    if st.get("tlm_note"):
                        print(f"        {st['tlm_note']}")
                    if (st["acked"] == 0 and not st["rig_received"] and diag is None and rig
                            and not args.no_diag):
                        diag = diagnose(gs, rig, rig_console)

            # leave both boards in the ICD default for the dashboard
            gs.command("PROFILE ICD", r"^OK,PROFILE", 2.0)
            gs.command("PA LOW", r"^OK,PA", 2.0)
            if rig and rig_console:
                rig.command("PROFILE ICD", r"^OK,PROFILE", 2.0)
                rig.command("PA LOW", r"^OK,PA", 2.0)
    finally:
        gs.close()
        if rig:
            rig.close()

    if all_rows:
        write_csv(out_dir / "smoke.csv", SMOKE_COLUMNS, all_rows)
        write_csv(out_dir / "sweep.csv", SWEEP_COLUMNS, runs)
        if plot(out_dir / "smoke.png", all_rows):
            print(f"  plot      -> {out_dir / 'smoke.png'}")
    if diag:
        (out_dir / "diagnosis.json").write_text(json.dumps(diag, indent=2), encoding="utf-8")
    summary = {
        "run_id": run_id, "gs_port": args.gs_port, "rig_port": args.rig_port,
        "limits": {"per_pct": PER_LIMIT_PCT, "rtt_max_ms": RTT_LIMIT_MS},
        "gates": [{"id": g.gid, "title": g.title, "pass": bool(g.passed), "detail": g.detail} for g in gates],
        "runs": runs, "diagnosis": diag,
        "overall": "PASS" if runs and all(r["result"] == "PASS" for r in runs)
        and all(g.passed for g in gates) else "FAIL",
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_summary_md(out_dir / "SUMMARY.md", run_id, args, gates, runs, diag)
    if all_rows:
        print(f"  smoke.csv -> {out_dir / 'smoke.csv'}  ({len(all_rows)} rows)")
    print(f"  OVERALL: {summary['overall']}")
    return 0 if summary["overall"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
