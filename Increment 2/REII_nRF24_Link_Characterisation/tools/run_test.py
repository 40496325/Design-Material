#!/usr/bin/env python3
"""Drive one distance point of the packet-loss characterisation.

Talks to the RX board (role rx) over serial, runs NOACK + ACK bursts for every requested
data rate and appends one CSV row per burst to results/packet_loss_<date>.csv.

    python tools/run_test.py --port /dev/ttyUSB0 --distance 5 --los 1 --pa MAX --n 1000
    python tools/run_test.py --port COM7 --distance 1 --pa LOW --rates 1000 --n 200      # smoke test
    python tools/run_test.py --port COM7 --distance 8 --los 0 --note "through wall" --channel 76
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import pathlib
import sys

from linktest_serial import LinkTestSerial

HERE = pathlib.Path(__file__).resolve().parent
RESULTS = HERE.parent / "results"

CSV_COLUMNS = [
    "timestamp", "distance_m", "los", "note", "channel", "rate_kbps", "pa", "mode", "n",
    "tx_sent", "rx_unique", "rx_dup", "rx_ooo", "tx_acked", "tx_failed", "retries_total",
    "retries_max", "rpd_hits", "rpd_samples", "duration_ms", "per_pct", "status",
]

# RESULT,run_id,mode,rate_kbps,ch,pa,n,tx_sent,rx_unique,dup,ooo,tx_acked,tx_failed,
#        retries_total,retries_max,rpd_hits,rpd_samples,duration_ms,status
RESULT_FIELDS = [
    "run_id", "mode", "rate_kbps", "channel", "pa", "n", "tx_sent", "rx_unique", "rx_dup",
    "rx_ooo", "tx_acked", "tx_failed", "retries_total", "retries_max", "rpd_hits",
    "rpd_samples", "duration_ms", "status",
]


def parse_result(line: str) -> dict[str, str]:
    parts = line.split(",")
    if parts[0] != "RESULT" or len(parts) != len(RESULT_FIELDS) + 1:
        raise ValueError(f"malformed RESULT line: {line!r}")
    return dict(zip(RESULT_FIELDS, parts[1:]))


def per_percent(r: dict[str, str]) -> float:
    n = int(r["n"])
    status = r["status"]
    if n == 0 or status == "RATE_UNSUPPORTED":
        return float("nan")
    if status == "CTRL_TIMEOUT":
        return 100.0
    if r["mode"] == "ACK" and status in ("OK", "TX_ABORTED"):
        # unsent packets after an abort count as failed
        return 100.0 * (1 - int(r["tx_acked"]) / n)
    return 100.0 * (1 - int(r["rx_unique"]) / n)


def burst_timeout_s(mode: str, n: int, spacing_us: int) -> float:
    # Mirrors firmware: ctrl retry 2 s + burst + report wait 5 s + margin.
    if mode == "ACK":
        return 2 + n * 0.050 + 1 + 5 + 3
    return 2 + n * spacing_us / 1e6 + 1.5 + 5 + 3


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port of the RX board")
    ap.add_argument("--distance", type=float, required=True, help="TX-RX distance in metres")
    ap.add_argument("--los", type=int, default=1, choices=(0, 1), help="1 = line of sight, 0 = obstructed")
    ap.add_argument("--note", default="", help="free text, e.g. 'through wall'")
    ap.add_argument("--channel", type=int, default=76, help="RF_CH for the bursts (0..125)")
    ap.add_argument("--rates", default="250,1000,2000", help="comma list of kbps values")
    ap.add_argument("--pa", default="MAX", choices=("MIN", "LOW", "HIGH", "MAX"))
    ap.add_argument("--modes", default="NOACK,ACK", help="comma list of NOACK / ACK")
    ap.add_argument("--n", type=int, default=1000, help="packets per burst (<= 2000)")
    ap.add_argument("--spacing-us", type=int, default=2000, help="NOACK packet spacing")
    ap.add_argument("--repeat", type=int, default=1, help="repeat every burst this many times")
    ap.add_argument("--out", default=None, help="CSV file (default results/packet_loss_<date>.csv)")
    ap.add_argument("--echo", action="store_true", help="print raw serial traffic to stderr")
    args = ap.parse_args()

    if not 1 <= args.n <= 2000:
        ap.error("--n must be 1..2000")
    if not 0 <= args.channel <= 125:
        ap.error("--channel must be 0..125")

    RESULTS.mkdir(exist_ok=True)
    out = pathlib.Path(args.out) if args.out else RESULTS / f"packet_loss_{dt.date.today().isoformat()}.csv"
    new_file = not out.exists()

    link = LinkTestSerial(args.port, echo=args.echo)
    if not link.wait_ready():
        print("RX board not answering (is role rx flashed? right port?)", file=sys.stderr)
        return 2

    link.send("PING")
    pong = link.wait_for(("PONG",), 4.0)
    if not pong or pong.split(",")[1] != "1":
        print("TX not reachable over the control link - check TX power / distance", file=sys.stderr)
        print("Continuing anyway; rows will be logged as CTRL_TIMEOUT.", file=sys.stderr)

    rates = [int(r) for r in args.rates.split(",") if r]
    modes = [m.strip().upper() for m in args.modes.split(",") if m]
    rows: list[dict[str, object]] = []
    with out.open("a", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=CSV_COLUMNS)
        if new_file:
            w.writeheader()
        for _ in range(args.repeat):
            for rate in rates:
                for mode in modes:
                    cmd = f"TEST {mode} {rate} {args.channel} {args.pa} {args.n} {args.spacing_us}"
                    link.send(cmd)
                    line = link.wait_for(("RESULT", "ERR"), burst_timeout_s(mode, args.n, args.spacing_us))
                    if line is None:
                        print(f"  {mode:5s} {rate:>4d} kbps: no RESULT (serial timeout)", file=sys.stderr)
                        continue
                    if line.startswith("ERR"):
                        print(f"  firmware rejected '{cmd}': {line}", file=sys.stderr)
                        continue
                    r = parse_result(line)
                    row: dict[str, object] = {
                        "timestamp": dt.datetime.now().isoformat(timespec="seconds"),
                        "distance_m": args.distance,
                        "los": args.los,
                        "note": args.note,
                        **{k: r[k] for k in CSV_COLUMNS if k in r},
                        "per_pct": f"{per_percent(r):.2f}",
                    }
                    w.writerow(row)
                    fh.flush()
                    rows.append(row)
                    print(
                        f"  {mode:5s} {rate:>4d} kbps ch{r['channel']} PA_{r['pa']}: "
                        f"PER={row['per_pct']}%  rx_unique={r['rx_unique']}/{r['n']}  "
                        f"acked={r['tx_acked']} failed={r['tx_failed']} retries={r['retries_total']} "
                        f"(max {r['retries_max']})  rpd={r['rpd_hits']}/{r['rpd_samples']}  {r['status']}"
                    )
    link.close()
    print(f"{len(rows)} rows appended to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
