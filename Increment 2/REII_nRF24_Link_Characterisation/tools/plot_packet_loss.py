#!/usr/bin/env python3
"""Plot packet loss vs distance from results/packet_loss_*.csv.

    python tools/plot_packet_loss.py results/packet_loss_2026-09-10.csv [more.csv ...]
    python tools/plot_packet_loss.py results/*.csv --pa MAX --out results/

Produces
  packet_loss_vs_distance.png      NOACK PER (%) vs distance, one line per data rate (LOS), NLOS as markers
  ack_reliability_vs_distance.png  ACK delivered (%) and mean retries/packet vs distance per rate
  summary.md                       table of every (distance, rate, mode) with mean PER over repeats
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

RATE_STYLE = {250: ("tab:green", "o"), 1000: ("tab:blue", "s"), 2000: ("tab:red", "^")}


def load(paths: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for p in paths:
        with open(p, newline="") as fh:
            rows.extend(csv.DictReader(fh))
    return rows


def f(x: str) -> float:
    try:
        return float(x)
    except ValueError:
        return float("nan")


def group(rows, mode, pa, los):
    """-> {rate: {distance: [per_pct, ...]}}"""
    out: dict[int, dict[float, list[float]]] = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r["mode"] != mode or r["pa"] != pa or int(r["los"]) != los:
            continue
        if r["status"] == "RATE_UNSUPPORTED":
            continue
        out[int(r["rate_kbps"])][float(r["distance_m"])].append(f(r["per_pct"]))
    return out


def mean_series(d: dict[float, list[float]]):
    xs = sorted(d)
    ys = [float(np.nanmean(d[x])) for x in xs]
    err = [float(np.nanstd(d[x])) if len(d[x]) > 1 else 0.0 for x in xs]
    return np.array(xs), np.array(ys), np.array(err)


def plot_per(rows, pa, outdir: pathlib.Path):
    fig, ax = plt.subplots(figsize=(8, 5))
    plotted = False
    for los, ls in ((1, "-"), (0, "")):
        g = group(rows, "NOACK", pa, los)
        for rate in sorted(g):
            xs, ys, err = mean_series(g[rate])
            color, marker = RATE_STYLE.get(rate, ("k", "x"))
            label = f"{rate} kbps" + (" LOS" if los else " NLOS")
            ax.errorbar(xs, ys, yerr=err, color=color, marker=marker, linestyle=ls or "none",
                        markerfacecolor=color if los else "none", capsize=3, label=label)
            plotted = True
    if not plotted:
        print("no NOACK rows for PA_" + pa, file=sys.stderr)
        return
    ax.axhline(1.0, color="grey", linestyle=":", linewidth=1)
    ax.text(ax.get_xlim()[1], 1.0, " 1 % PER", va="bottom", ha="right", color="grey", fontsize=8)
    ax.set_xlabel("distance TX–RX [m]")
    ax.set_ylabel("packet error rate [%]  (no ACK, 32 B payload)")
    ax.set_title(f"nRF24L01+ PA+LNA — packet loss vs distance, PA_{pa}")
    ax.set_ylim(bottom=-1)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "packet_loss_vs_distance.png", dpi=150)
    print("wrote", outdir / "packet_loss_vs_distance.png")


def plot_ack(rows, pa, outdir: pathlib.Path):
    per_rate: dict[int, dict[float, list[tuple[float, float]]]] = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r["mode"] != "ACK" or r["pa"] != pa or int(r["los"]) != 1:
            continue
        if r["status"] in ("RATE_UNSUPPORTED", "CTRL_TIMEOUT"):
            continue
        sent = f(r["tx_sent"])
        retries_per_pkt = f(r["retries_total"]) / sent if sent > 0 else float("nan")
        per_rate[int(r["rate_kbps"])][float(r["distance_m"])].append((100 - f(r["per_pct"]), retries_per_pkt))
    if not per_rate:
        print("no ACK rows for PA_" + pa, file=sys.stderr)
        return
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    for rate in sorted(per_rate):
        color, marker = RATE_STYLE.get(rate, ("k", "x"))
        xs = sorted(per_rate[rate])
        deliv = [np.nanmean([v[0] for v in per_rate[rate][x]]) for x in xs]
        retr = [np.nanmean([v[1] for v in per_rate[rate][x]]) for x in xs]
        ax1.plot(xs, deliv, color=color, marker=marker, label=f"{rate} kbps")
        ax2.plot(xs, retr, color=color, marker=marker, label=f"{rate} kbps")
    ax1.set_ylabel("delivered with ACK+retry [%]")
    ax1.axhline(99.9, color="grey", linestyle=":", linewidth=1)
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    ax1.set_title(f"Enhanced ShockBurst reliability (ARD 1500 µs, ARC 15), PA_{pa}, LOS")
    ax2.set_ylabel("mean retransmits per packet")
    ax2.set_xlabel("distance TX–RX [m]")
    ax2.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(outdir / "ack_reliability_vs_distance.png", dpi=150)
    print("wrote", outdir / "ack_reliability_vs_distance.png")


def write_summary(rows, outdir: pathlib.Path):
    agg: dict[tuple, list[dict[str, str]]] = defaultdict(list)
    for r in rows:
        agg[(float(r["distance_m"]), int(r["los"]), r["pa"], int(r["rate_kbps"]), r["mode"])].append(r)
    lines = ["| distance [m] | LOS | PA | rate [kbps] | mode | runs | mean PER [%] | mean retries/pkt | max retries | status |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for key in sorted(agg):
        rs = agg[key]
        per = np.nanmean([f(r["per_pct"]) for r in rs])
        sent = sum(f(r["tx_sent"]) for r in rs)
        retr = sum(f(r["retries_total"]) for r in rs) / sent if sent else float("nan")
        rmax = max(int(r["retries_max"]) for r in rs)
        statuses = ",".join(sorted({r["status"] for r in rs}))
        d, los, pa, rate, mode = key
        lines.append(f"| {d:g} | {los} | {pa} | {rate} | {mode} | {len(rs)} | {per:.2f} | {retr:.2f} | {rmax} | {statuses} |")
    (outdir / "summary.md").write_text("\n".join(lines) + "\n")
    print("wrote", outdir / "summary.md")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--pa", default="MAX", choices=("MIN", "LOW", "HIGH", "MAX"), help="PA level to plot")
    ap.add_argument("--out", default=None, help="output directory (default: directory of first csv)")
    args = ap.parse_args()
    rows = load(args.csv)
    if not rows:
        print("no rows", file=sys.stderr)
        return 1
    outdir = pathlib.Path(args.out) if args.out else pathlib.Path(args.csv[0]).resolve().parent
    outdir.mkdir(parents=True, exist_ok=True)
    plot_per(rows, args.pa, outdir)
    plot_ack(rows, args.pa, outdir)
    write_summary(rows, outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
