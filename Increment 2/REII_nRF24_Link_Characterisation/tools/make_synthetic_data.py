#!/usr/bin/env python3
"""Generate fake but plausible results so the plotting / analysis scripts can be exercised
before any hardware measurement exists. Writes into a directory of your choice (default
results/synthetic/) — never commit the synthetic output as if it were measured.

    python tools/make_synthetic_data.py --out results/synthetic
    python tools/plot_packet_loss.py results/synthetic/packet_loss_synthetic.csv
    python tools/channel_analysis.py --infile results/synthetic/channel_survey_synthetic.raw --out results/synthetic
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import pathlib
import random

from run_test import CSV_COLUMNS

HERE = pathlib.Path(__file__).resolve().parent

# rough range at which PER reaches 50 % (LOS, PA_MAX) per rate - purely illustrative
R50 = {250: 60.0, 1000: 40.0, 2000: 28.0}
DIST = [1, 2, 5, 10, 15, 20, 30, 50]


def per_model(rate: int, d: float, los: int) -> float:
    r50 = R50[rate] * (0.45 if not los else 1.0)
    x = (d / r50) ** 4
    return min(100.0, 100.0 * x / (1 + x))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE.parent / "results" / "synthetic"))
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    random.seed(args.seed)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    n = 1000
    with (out / "packet_loss_synthetic.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=CSV_COLUMNS)
        w.writeheader()
        points = [(d, 1, "") for d in DIST] + [(6, 0, "through wall"), (12, 0, "corner"), (20, 0, "two walls")]
        for d, los, note in points:
            for rate in (250, 1000, 2000):
                per = per_model(rate, d, los)
                per_meas = min(100.0, max(0.0, per + random.gauss(0, 0.5 + per * 0.05)))
                rx_unique = round(n * (1 - per_meas / 100))
                w.writerow({
                    "timestamp": dt.datetime.now().isoformat(timespec="seconds"), "distance_m": d, "los": los,
                    "note": note, "channel": 76, "rate_kbps": rate, "pa": "MAX", "mode": "NOACK", "n": n,
                    "tx_sent": n, "rx_unique": rx_unique, "rx_dup": 0, "rx_ooo": 0, "tx_acked": 0, "tx_failed": 0,
                    "retries_total": 0, "retries_max": 0, "rpd_hits": round(2000 * max(0, 1 - d / 20)),
                    "rpd_samples": 2000 + n * 2, "duration_ms": n * 2, "per_pct": f"{per_meas:.2f}", "status": "OK",
                })
                # ACK mode: each packet gets up to 16 tries at loss probability p
                p = per / 100.0
                failed = round(n * p ** 16)
                acked = n - failed
                mean_retries = (p - p ** 16) / (1 - p) if p < 1 else 15
                w.writerow({
                    "timestamp": dt.datetime.now().isoformat(timespec="seconds"), "distance_m": d, "los": los,
                    "note": note, "channel": 76, "rate_kbps": rate, "pa": "MAX", "mode": "ACK", "n": n,
                    "tx_sent": n, "rx_unique": acked, "rx_dup": round(acked * p * 0.3), "rx_ooo": 0,
                    "tx_acked": acked, "tx_failed": failed, "retries_total": round(n * mean_retries),
                    "retries_max": min(15, round(mean_retries * 4 + 1)) if p > 0.001 else 0,
                    "rpd_hits": 0, "rpd_samples": 0, "duration_ms": round(n * (1.5 + mean_retries * 3)),
                    "per_pct": f"{100 * failed / n:.2f}", "status": "OK",
                })

    sweeps = 200
    lines = []
    for ch in range(126):
        occ = 0.02
        for centre, strength in ((12, 0.35), (37, 0.6), (62, 0.25), (76, 0.5)):  # WiFi 1/6/11 + another group's nRF on 76
            width = 11 if centre != 76 else 1
            if abs(ch - centre) <= width:
                occ += strength * (1 - abs(ch - centre) / (width + 1))
        hits = sum(1 for _ in range(sweeps) if random.random() < occ)
        lines.append(f"RPD,{ch},{hits},{sweeps}")
    lines += [
        "WIFI,eduroam,aa:bb:cc:00:00:01,1,-52",
        "WIFI,NWU-Lab,aa:bb:cc:00:00:02,6,-41",
        "WIFI,eduroam,aa:bb:cc:00:00:03,6,-67",
        "WIFI,Printer-3F,aa:bb:cc:00:00:04,11,-75",
        "WIFI,<hidden>,aa:bb:cc:00:00:05,13,-88",
    ]
    (out / "channel_survey_synthetic.raw").write_text("\n".join(lines) + "\n")
    print("wrote", out / "packet_loss_synthetic.csv", "and", out / "channel_survey_synthetic.raw")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
