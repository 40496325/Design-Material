#!/usr/bin/env python3
"""
REII I3 bench evidence capture — dual-port serial logger.

Captures everything needed for the Increment 3 bench-exit gate
(see docs/27_reii_nrf_wiring_and_config.md §5 steps 0-5):

  Ground station (NodeMCU-32S, USB-CDC @ 115200):
    1. ECHO          -> proves USB-CDC before touching RF
    2. INFO          -> radio.begin() OK + isPVariant + printPrettyDetails
    3. PING          -> PONG,1,<ms> round-trip vs the rig
    4. APP           -> switch to ch77/2M/PA_MAX/ARC5/ARD500
    5. smoke burst   -> N=200 packets at app config (CSV)

  Rig (ESP32-S3, USB CDC / UART @ 115200):
    - boot banner (ESP-IDF version, flash size, peripherals)
    - nRF24 init (probe OK, configure OK, listening on REII2 ch77/2Mbps)
    - nrf: PING #N from groundstation  (link-up evidence on crane side)
    - nrf: listening - frames=N        (periodic heartbeat)

Both ports are captured simultaneously with monotonic timestamps.
All artefacts are written to results/i3_bench/<run_id>/ with a
machine-readable index.json plus human-readable logs.

Usage:
    python tools/capture_evidence.py --gs-port COM10 --rig-port COM12 --smoke-n 200

Prerequisites:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
import signal
import sys
import threading
import time
from datetime import datetime, timezone

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    sys.exit(2)

# ---------------------------------------------------------------------------
# Addresses / protocol constants (must match both firmware images)
# ---------------------------------------------------------------------------
NRF_MAGIC_CMD = 0xA5
NRF_CMD_PING = 0x01
PAYLOAD_SIZE = 32

# ---------------------------------------------------------------------------
# Serial reader thread
# ---------------------------------------------------------------------------

class SerialCapture:
    """Reads one serial port, timestamps every line, writes to a log file."""

    def __init__(self, name: str, port: str, baud: int, log_path: pathlib.Path,
                 echo: bool = True):
        self.name = name            # "groundstation" or "rig"
        self.port = port
        self.baud = baud
        self.log_path = log_path
        self.echo = echo
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._ser: serial.Serial | None = None
        self.lines: list[str] = []  # timestamped lines for in-RAM summary
        self._log_fh = None

    def open(self) -> bool:
        try:
            self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
            time.sleep(0.5)
            self._ser.reset_input_buffer()
            self._log_fh = self.log_path.open("w", encoding="utf-8", errors="replace")
            self._thread = threading.Thread(target=self._reader, daemon=True)
            self._thread.start()
            self._log(f"[CAPTURE START] port={self.port} baud={self.baud}")
            return True
        except serial.SerialException as exc:
            print(f"[{self.name}] cannot open {self.port}: {exc}", file=sys.stderr)
            return False

    def _reader(self) -> None:
        assert self._ser is not None
        buf = bytearray()
        while not self._stop.is_set():
            try:
                data = self._ser.read(self._ser.in_waiting or 1)
            except (serial.SerialException, OSError):
                break
            if not data:
                continue
            buf.extend(data)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip("\r")
                if text:
                    self._log(text)

    def _log(self, text: str) -> None:
        ts = time.monotonic()
        iso = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        entry = f"{iso}  t+{ts:9.3f}  {text}"
        self.lines.append(entry)
        if self.echo:
            print(entry)
        if self._log_fh:
            self._log_fh.write(entry + "\n")
            self._log_fh.flush()

    def write(self, cmd: str) -> None:
        if self._ser:
            self._ser.write((cmd.strip() + "\n").encode())
            self._ser.flush()
            self._log(f">>> {cmd}")

    def close(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._log_fh:
            self._log("[CAPTURE END]")
            self._log_fh.close()
        if self._ser:
            try:
                self._ser.close()
            except serial.SerialException:
                pass

    def wait_for(self, prefixes: tuple[str, ...], timeout_s: float) -> str | None:
        """Return the first captured line starting with any prefix, else None."""
        deadline = time.monotonic() + timeout_s
        seen = len(self.lines)
        while time.monotonic() < deadline:
            for i in range(seen, len(self.lines)):
                entry = self.lines[i]
                parts = entry.split("  ", 2)
                if len(parts) == 3:
                    text = parts[2]
                else:
                    text = entry
                if text.startswith(prefixes):
                    return text
            time.sleep(0.02)
        return None

    def wait_for_regex(self, pattern: str, timeout_s: float) -> str | None:
        """Return the first captured line matching `pattern`, else None."""
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout_s
        seen = len(self.lines)
        while time.monotonic() < deadline:
            for i in range(seen, len(self.lines)):
                entry = self.lines[i]
                parts = entry.split("  ", 2)
                text = parts[2] if len(parts) == 3 else entry
                if rx.search(text):
                    return text
            time.sleep(0.02)
        return None


# ---------------------------------------------------------------------------
# Bring-up sequence runners
# ---------------------------------------------------------------------------


def bring_up_groundstation(gs: SerialCapture, timeout: float = 15.0) -> dict:
    """Run ECHO -> INFO -> PING -> APP on the ground station. Returns results."""
    results: dict = {}

    # Step 1: ECHO (prove USB-CDC)
    gs.write("ECHO bringup_evidence")
    line = gs.wait_for(("ECHO,bringup_evidence", "ECHO,"), timeout)
    results["echo_ok"] = line is not None
    results["echo_line"] = line

    # Step 2: INFO (radio begin + register dump)
    gs.write("INFO")
    line = gs.wait_for(("radio.begin() OK",), timeout)
    results["radio_begin_ok"] = line is not None
    results["radio_begin_line"] = line

    # Wait for printPrettyDetails to finish (look for READY banner)
    line = gs.wait_for(("READY",), timeout)
    results["ready_after_info"] = line is not None
    results["ready_line"] = line

    # Extract key register values from the INFO capture
    info_text = "\n".join(gs.lines)
    results["is_pvariant"] = bool(re.search(r"isPVariant\s*=\s*1", info_text))
    results["static_payload_32"] = bool(re.search(r"Static Payload Length\s*=\s*32 bytes", info_text))
    results["crc16"] = bool(re.search(r"CRC Length\s*=\s*16 bits", info_text))
    results["addr_len_5"] = bool(re.search(r"Address Length\s*=\s*5 bytes", info_text))
    results["auto_ack_enabled"] = bool(re.search(r"Auto Acknowledgment\s*=\s*Enabled", info_text))
    results["dynamic_payloads_off"] = bool(re.search(r"Dynamic Payloads\s*=\s*Disabled", info_text))

    # Step 3: PING (round trip vs rig)
    gs.write("PING")
    line = gs.wait_for(("PONG,",), timeout)
    results["ping_ok"] = line is not None
    results["pong_line"] = line
    if line:
        m = re.search(r"PONG,(\d+),(\d+)", line)
        results["pong_ack"] = int(m.group(1)) if m else None
        results["pong_rtt_ms"] = int(m.group(2)) if m else None

    # Step 4: APP (switch to application baseline ch77/2M/PA_MAX/ARC5/ARD500)
    gs.write("APP")
    line = gs.wait_for(("OK,app_ch=77",), timeout)
    results["app_ok"] = line is not None
    results["app_line"] = line

    return results


def run_smoke(gs: SerialCapture, rig: SerialCapture, n: int = 200,
              timeout_per_ping: float = 3.0) -> dict:
    """
    Run N PING/PONG rounds at the app config, logging round-trip times.
    Sends PING from ground station, expects PONG back; rig side logs receipt.
    Returns summary stats + per-round rows.
    """
    rows: list[dict] = []
    successes = 0
    failures = 0
    rtts: list[int] = []

    for i in range(1, n + 1):
        gs.write("PING")
        line = gs.wait_for(("PONG,",), timeout_per_ping)
        if line:
            m = re.search(r"PONG,(\d+),(\d+)", line)
            ack = int(m.group(1)) if m else 0
            rtt = int(m.group(2)) if m else -1
            if ack == 1:
                successes += 1
                rtts.append(rtt)
            else:
                failures += 1
        else:
            ack = 0
            rtt = -1
            failures += 1
        rows.append({
            "round": i,
            "ack": ack,
            "rtt_ms": rtt,
            "timestamp": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
        })
        if i % 25 == 0:
            print(f"  smoke {i}/{n}: ack={successes} fail={failures}")
        time.sleep(0.05)  # small gap between PINGs

    # Rig-side: capture how many PINGs it saw
    rig_text = "\n".join(rig.lines)
    rig_ping_count = len(re.findall(r"nrf:\s*PING\s+#\d+\s+from groundstation", rig_text))
    rig_frames = None
    m = re.search(r"nrf:\s*listening\s*-\s*frames=(\d+)", rig_text)
    if m:
        rig_frames = int(m.group(1))

    summary = {
        "n": n,
        "successes": successes,
        "failures": failures,
        "per_pct": round(100.0 * failures / n, 2) if n else 0.0,
        "rtt_min_ms": min(rtts) if rtts else None,
        "rtt_max_ms": max(rtts) if rtts else None,
        "rtt_avg_ms": round(sum(rtts) / len(rtts), 2) if rtts else None,
        "rig_ping_count": rig_ping_count,
        "rig_frames": rig_frames,
        "rows": rows,
    }
    return summary


def capture_rig_baseline(rig: SerialCapture, timeout: float = 20.0) -> dict:
    """Wait for the rig boot banner + nRF24 bring-up to complete."""
    results: dict = {}

    # Wait for nRF24: crane link up banner
    line = rig.wait_for(("nRF24: crane link up",), timeout)
    results["link_up"] = line is not None
    results["link_up_line"] = line

    # Extract key config from the rig capture
    rig_text = "\n".join(rig.lines)
    results["esp32s3_boot"] = bool(re.search(r"Hello world from esp32s3", rig_text))
    results["idf_v61"] = bool(re.search(r"ESP-IDF v6\.1", rig_text))
    results["flash_16mb"] = bool(re.search(r"SPI Flash Size\s*:\s*16MB", rig_text))
    results["ads1220_ok"] = bool(re.search(r"ADS1220 responding", rig_text))
    results["spwm_running"] = bool(re.search(r"SPWM", rig_text))
    results["nrf_probe_ok"] = bool(re.search(r"nRF24: crane link up|isPVariant|probe", rig_text))
    results["listening_reii2"] = bool(re.search(r"PRX on 'REII2'", rig_text))
    results["ch77_2mbps"] = bool(re.search(r"ch77.*2Mbps|ch77/2Mbps", rig_text))

    return results



# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def list_ports() -> None:
    ports = serial.tools.list_ports.comports()
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device}  —  {p.description}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Capture REII I3 bench evidence from both serial ports.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--gs-port", required=True,
                    help="Ground-station (NodeMCU) serial port, e.g. COM10")
    ap.add_argument("--rig-port", required=True,
                    help="Rig (ESP32-S3) serial port, e.g. COM12")
    ap.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    ap.add_argument("--smoke-n", type=int, default=200,
                    help="Number of PING/PONG rounds in the smoke burst")
    ap.add_argument("--smoke-timeout", type=float, default=3.0,
                    help="Per-PING timeout in seconds")
    ap.add_argument("--out", default=None,
                    help="Output directory (default results/i3_bench/<run_id>/)")
    ap.add_argument("--list-ports", action="store_true", help="List serial ports and exit")
    ap.add_argument("--no-echo", action="store_true",
                    help="Do not print captured lines to console")
    args = ap.parse_args()

    if args.list_ports:
        list_ports()
        return 0

    # --- output directory ---------------------------------------------------
    run_id = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    base = pathlib.Path(args.out) if args.out else (
        pathlib.Path(__file__).resolve().parent.parent / "results" / "i3_bench"
    )
    run_dir = base / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    print(f"Evidence directory: {run_dir}")

    # --- open both ports ----------------------------------------------------
    gs = SerialCapture("groundstation", args.gs_port, args.baud,
                       run_dir / "groundstation.log", echo=not args.no_echo)
    rig = SerialCapture("rig", args.rig_port, args.baud,
                        run_dir / "rig.log", echo=not args.no_echo)

    if not gs.open():
        return 2
    if not rig.open():
        gs.close()
        return 2

    # Ctrl+C handler: close both ports cleanly
    def stop(signum, frame):
        print("\n[abort] closing ports…")
        gs.close()
        rig.close()
        sys.exit(130)
    signal.signal(signal.SIGINT, stop)

    evidence: dict = {
        "run_id": run_id,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "gs_port": args.gs_port,
        "rig_port": args.rig_port,
        "baud": args.baud,
        "steps": {},
    }

    try:
        # ---- Ground-station bring-up -------------------------------------
        print("\n=== Ground station: ECHO -> INFO -> PING -> APP ===")
        gs_results = bring_up_groundstation(gs, timeout=15.0)
        evidence["steps"]["groundstation_bringup"] = gs_results

        # ---- Rig baseline capture (already running, just capture) -------
        print("\n=== Rig: waiting for nRF24 link-up banner ===")
        rig_results = capture_rig_baseline(rig, timeout=20.0)
        evidence["steps"]["rig_baseline"] = rig_results

        # ---- Smoke burst at app config -----------------------------------
        print(f"\n=== Smoke burst: N={args.smoke_n} PING/PONG at app config ===")
        smoke = run_smoke(gs, rig, n=args.smoke_n,
                          timeout_per_ping=args.smoke_timeout)
        evidence["steps"]["smoke"] = smoke

        # Write smoke CSV
        smoke_csv = run_dir / "smoke.csv"
        with smoke_csv.open("w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=["round", "ack", "rtt_ms", "timestamp"])
            w.writeheader()
            for row in smoke["rows"]:
                w.writerow(row)
        print(f"Smoke CSV written: {smoke_csv}")


        # ---- Summary ------------------------------------------------------
        all_ok = (
            gs_results.get("echo_ok", False)
            and gs_results.get("radio_begin_ok", False)
            and gs_results.get("ping_ok", False)
            and gs_results.get("pong_ack") == 1
            and gs_results.get("app_ok", False)
            and rig_results.get("link_up", False)
            and smoke["failures"] == 0
        )
        evidence["overall_pass"] = all_ok
        evidence["summary"] = {
            "groundstation_echo_ok": gs_results.get("echo_ok", False),
            "groundstation_radio_begin_ok": gs_results.get("radio_begin_ok", False),
            "groundstation_is_pvariant": gs_results.get("is_pvariant", False),
            "groundstation_static_payload_32": gs_results.get("static_payload_32", False),
            "groundstation_crc16": gs_results.get("crc16", False),
            "groundstation_addr_len_5": gs_results.get("addr_len_5", False),
            "groundstation_auto_ack_enabled": gs_results.get("auto_ack_enabled", False),
            "groundstation_ping_ok": gs_results.get("ping_ok", False),
            "groundstation_pong_ack": gs_results.get("pong_ack"),
            "groundstation_pong_rtt_ms": gs_results.get("pong_rtt_ms"),
            "groundstation_app_ok": gs_results.get("app_ok", False),
            "rig_link_up": rig_results.get("link_up", False),
            "rig_listening_reii2": rig_results.get("listening_reii2", False),
            "rig_ch77_2mbps": rig_results.get("ch77_2mbps", False),
            "smoke_n": smoke["n"],
            "smoke_successes": smoke["successes"],
            "smoke_failures": smoke["failures"],
            "smoke_per_pct": smoke["per_pct"],
            "smoke_rtt_min_ms": smoke["rtt_min_ms"],
            "smoke_rtt_max_ms": smoke["rtt_max_ms"],
            "smoke_rtt_avg_ms": smoke["rtt_avg_ms"],
            "rig_ping_count": smoke["rig_ping_count"],
            "rig_frames": smoke["rig_frames"],
            "overall_pass": all_ok,
        }

        # Write index.json (machine-readable evidence index)
        index_path = run_dir / "index.json"
        with index_path.open("w", encoding="utf-8") as fh:
            json.dump(evidence, fh, indent=2)
        print(f"\nEvidence index written: {index_path}")

        # Write human-readable summary
        summary_path = run_dir / "SUMMARY.md"
        lines = [
            f"# REII I3 Bench Evidence — {run_id}",
            f"",
            f"## Overall result: {'PASS' if all_ok else 'FAIL'}",
            f"",
            f"### Ground station (NodeMCU-32S, {args.gs_port})",
            f"- USB-CDC echo: {'OK' if gs_results.get('echo_ok') else 'FAIL'}",
            f"- radio.begin() OK: {'OK' if gs_results.get('radio_begin_ok') else 'FAIL'}",
            f"- isPVariant=1: {'OK' if gs_results.get('is_pvariant') else 'FAIL'}",
            f"- Static 32-byte payload: {'OK' if gs_results.get('static_payload_32') else 'FAIL'}",
            f"- CRC-16: {'OK' if gs_results.get('crc16') else 'FAIL'}",
            f"- 5-byte address: {'OK' if gs_results.get('addr_len_5') else 'FAIL'}",
            f"- Auto-ACK enabled: {'OK' if gs_results.get('auto_ack_enabled') else 'FAIL'}",
            f"- PING->PONG: {'OK' if gs_results.get('ping_ok') else 'FAIL'}",
            f"  - PONG ack=1: {'OK' if gs_results.get('pong_ack') == 1 else 'FAIL'}",
            f"  - RTT: {gs_results.get('pong_rtt_ms')} ms",
            f"- APP config (ch77/2M/PA_MAX/ARC5/ARD500): {'OK' if gs_results.get('app_ok') else 'FAIL'}",
            f"",
            f"### Rig (ESP32-S3, {args.rig_port})",
            f"- nRF24 link up (listening on REII2): {'OK' if rig_results.get('link_up') else 'FAIL'}",
            f"- PRX on 'REII2': {'OK' if rig_results.get('listening_reii2') else 'FAIL'}",
            f"- ch77 / 2 Mbps: {'OK' if rig_results.get('ch77_2mbps') else 'FAIL'}",
            f"- PINGs received by rig: {smoke['rig_ping_count']}",
            f"",
            f"### Smoke burst (N={smoke['n']} PING/PONG at app config)",
            f"- Successes: {smoke['successes']}",
            f"- Failures: {smoke['failures']}",
            f"- PER: {smoke['per_pct']}%",
            f"- RTT min/max/avg: {smoke['rtt_min_ms']} / {smoke['rtt_max_ms']} / {smoke['rtt_avg_ms']} ms",
            f"",
            f"### Artefacts",
            f"- `groundstation.log` — full ground-station serial capture (timestamped)",
            f"- `rig.log` — full rig serial capture (timestamped)",
            f"- `smoke.csv` — per-round PING/PONG results",
            f"- `index.json` — machine-readable evidence index",
            f"",
        ]
        with summary_path.open("w", encoding="utf-8") as fh:
            fh.write("\n".join(lines))
        print(f"Summary written: {summary_path}")

        print(f"\n{'=' * 60}")
        print(f"OVERALL: {'PASS' if all_ok else 'FAIL'}")
        print(f"{'=' * 60}")

        return 0 if all_ok else 1

    finally:
        gs.close()
        rig.close()


if __name__ == "__main__":
    sys.exit(main())
