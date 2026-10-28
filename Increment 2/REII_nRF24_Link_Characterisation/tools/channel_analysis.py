#!/usr/bin/env python3
"""Channel survey: capture the scanner firmware output, score every nRF24 channel, recommend.

Capture from the board (role scanner flashed on the NodeMCU-32S):
    python tools/channel_analysis.py --port /dev/ttyUSB0 --sweeps 200 --label "lab, all groups on"

Re-analyse a saved capture:
    python tools/channel_analysis.py --infile results/channel_survey_2026-09-10_1432.raw

Outputs (results/):
    channel_survey_<stamp>.raw   verbatim serial lines (RPD,... / WIFI,... )
    channel_survey_<stamp>.csv   per-channel table: ch, freq_mhz, rpd_occupancy, wifi_penalty, score, in_ism
    channel_survey_<stamp>.png   occupancy bars + WiFi overlay + recommendation
and prints the top channels plus a multi-group channel plan.

Method: docs/04_channel_selection.md.
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
RESULTS = HERE.parent / "results"

NUM_CH = 126
ISM_BOTTOM_CH = 2              # keep a 2 MHz-wide (2 Mbps) signal inside 2400.0-2483.5 MHz
ISM_TOP_CH = 81
WIFI_HALF_BW_MHZ = 11.0        # 20/22 MHz channel
CONTROL_CH = 80                # firmware control channel (protocol.h CTRL_CHANNEL); excluded from the recommendation


def wifi_center_mhz(wifi_ch: int) -> float:
    if wifi_ch == 14:
        return 2484.0
    return 2407.0 + 5.0 * wifi_ch


def nrf_freq_mhz(ch: int) -> float:
    return 2400.0 + ch


def capture(port: str, sweeps: int) -> list[str]:
    from linktest_serial import LinkTestSerial

    link = LinkTestSerial(port)
    link.send(f"SCAN {sweeps}")
    lines: list[str] = []
    got_start = False
    t_limit = 60 + sweeps * 126 * 0.0005 * 1.5  # dwell + SPI overhead, generous
    import time

    t0 = time.monotonic()
    while time.monotonic() - t0 < t_limit:
        line = link.readline()
        if line is None:
            continue
        if line.startswith("SCAN_START"):
            got_start = True
            lines = []
            continue
        if not got_start:
            continue
        if line == "END":
            break
        if line.startswith(("RPD,", "WIFI,")):
            lines.append(line)
    link.close()
    if not lines:
        raise SystemExit("no scanner output - is the scanner role flashed?")
    return lines


def parse(lines: list[str]):
    rpd = np.zeros(NUM_CH)
    sweeps = 1
    wifi: list[tuple[str, str, int, int]] = []
    for line in lines:
        parts = line.strip().split(",")
        if parts[0] == "RPD" and len(parts) == 4:
            ch, hits, sw = int(parts[1]), int(parts[2]), int(parts[3])
            if 0 <= ch < NUM_CH:
                rpd[ch] = hits
                sweeps = max(sweeps, sw)
        elif parts[0] == "WIFI" and len(parts) >= 5:
            ssid, bssid, ch, rssi = parts[1], parts[2], int(parts[3]), int(parts[4])
            wifi.append((ssid, bssid, ch, rssi))
    return rpd / sweeps, wifi, sweeps


def wifi_penalty(wifi) -> np.ndarray:
    pen = np.zeros(NUM_CH)
    freqs = np.array([nrf_freq_mhz(c) for c in range(NUM_CH)])
    for _ssid, _bssid, wch, rssi in wifi:
        centre = wifi_center_mhz(wch)
        weight = 10 ** ((min(max(rssi, -100), -30) + 100) / 20.0)  # -100 dBm -> 1, -40 dBm -> 1000
        d = np.abs(freqs - centre)
        # flat inside +-9 MHz, raised-cosine roll-off to 0 at +-11 MHz (a bit beyond to be safe)
        overlap = np.where(d <= 9.0, 1.0, np.where(d >= 13.0, 0.0, 0.5 * (1 + np.cos(np.pi * (d - 9.0) / 4.0))))
        pen += weight * overlap
    return pen


def score_channels(rpd_occ: np.ndarray, wifi):
    wp = wifi_penalty(wifi)
    wp_norm = wp / wp.max() if wp.max() > 0 else wp
    score = 0.5 * wp_norm + 0.5 * rpd_occ
    return score, wp


def guard_ok(ch: int, score: np.ndarray, guard: int, thresh: float, top_ch: int) -> bool:
    lo, hi = ch - guard, ch + guard
    if lo < ISM_BOTTOM_CH or hi > top_ch:
        return False
    return bool(np.all(score[lo:hi + 1] <= thresh))


def recommend(score: np.ndarray, guard: int, plan: int, allow_above_ism: bool):
    top_ch = NUM_CH - 1 if allow_above_ism else ISM_TOP_CH
    cand = [c for c in range(ISM_BOTTOM_CH, top_ch + 1) if c != CONTROL_CH]
    ranked = sorted(cand, key=lambda c: (score[c], c))
    best10 = ranked[:10]

    # single-link pick: the channel itself and +-half the group spacing must all be in the quiet 30 %
    half = max(1, guard // 2)
    thresh = np.percentile(score[ISM_BOTTOM_CH: top_ch + 1], 30)
    guarded = [c for c in ranked if guard_ok(c, score, half, thresh, top_ch)]
    best_guarded = guarded[0] if guarded else ranked[0]

    # multi-group plan: greedily pick channels >= `guard` apart, quietest first
    plan_chs: list[int] = []
    for c in ranked:
        if all(abs(c - p) >= guard for p in plan_chs):
            plan_chs.append(c)
        if len(plan_chs) >= plan:
            break
    return best10, best_guarded, sorted(plan_chs), bool(guarded), half


def plot(rpd_occ, wifi, score, best, plan_chs, out_png: pathlib.Path, label: str):
    fig, ax = plt.subplots(figsize=(12, 5.5))
    chs = np.arange(NUM_CH)
    ax.bar(chs, rpd_occ * 100, width=0.9, color="tab:blue", alpha=0.7, label="nRF24 RPD occupancy [% of sweeps]")
    ax.set_xlabel("nRF24 channel  (F = 2400 + ch MHz)")
    ax.set_ylabel("RPD hits [% of sweeps]  (signal > −64 dBm)")
    ax.set_xlim(-1, NUM_CH)
    ax.set_ylim(0, max(5, float(rpd_occ.max() * 100) * 1.15))
    ax.axvspan(ISM_TOP_CH + 0.5, NUM_CH, color="grey", alpha=0.15, label="at/above ISM band edge (2483.5 MHz)")

    ax2 = ax.twinx()
    for ssid, _bssid, wch, rssi in wifi:
        c = wifi_center_mhz(wch) - 2400
        ax2.add_patch(plt.Rectangle((c - WIFI_HALF_BW_MHZ, -100), 2 * WIFI_HALF_BW_MHZ, rssi + 100,
                                    color="tab:orange", alpha=0.12))
        ax2.plot([c - WIFI_HALF_BW_MHZ, c + WIFI_HALF_BW_MHZ], [rssi, rssi], color="tab:orange", lw=1)
        ax2.text(c, rssi + 1, f"{ssid[:12]} ch{wch}", ha="center", fontsize=6, color="tab:orange")
    ax2.set_ylim(-100, -20)
    ax2.set_ylabel("WiFi AP RSSI [dBm]  (bars = ±11 MHz)", color="tab:orange")

    ax.axvline(best, color="tab:green", lw=2, label=f"recommended ch {best} ({nrf_freq_mhz(best):.0f} MHz)")
    for p in plan_chs:
        if p != best:
            ax.axvline(p, color="tab:green", lw=1, ls="--")
    ax.axvline(CONTROL_CH, color="k", lw=1, ls=":", label=f"control ch {CONTROL_CH}")
    ax.set_title(f"nRF24 channel survey — {label}")
    ax.legend(loc="upper left", fontsize=8)
    fig.tight_layout()
    fig.savefig(out_png, dpi=150)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port of the scanner board")
    src.add_argument("--infile", help="re-analyse a saved .raw capture")
    ap.add_argument("--sweeps", type=int, default=200)
    ap.add_argument("--label", default="", help="free text for the plot title / filename")
    ap.add_argument("--guard", type=int, default=4, help="min channel spacing between groups")
    ap.add_argument("--plan", type=int, default=5, help="number of simultaneous links to plan for")
    ap.add_argument("--allow-above-ism", action="store_true", help="also consider channels 84..125")
    ap.add_argument("--out", default=None, help="output directory (default results/)")
    args = ap.parse_args()

    outdir = pathlib.Path(args.out) if args.out else RESULTS
    outdir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y-%m-%d_%H%M")

    if args.port:
        lines = capture(args.port, args.sweeps)
        raw = outdir / f"channel_survey_{stamp}.raw"
        raw.write_text("\n".join(lines) + "\n")
        print("saved", raw)
        base = raw.with_suffix("")
    else:
        raw = pathlib.Path(args.infile)
        lines = raw.read_text().splitlines()
        base = outdir / raw.stem

    rpd_occ, wifi, sweeps = parse(lines)
    score, wp = score_channels(rpd_occ, wifi)
    best10, best, plan_chs, guarded_found, half = recommend(score, args.guard, args.plan, args.allow_above_ism)

    with open(base.with_suffix(".csv"), "w") as fh:
        fh.write("ch,freq_mhz,rpd_occupancy,wifi_penalty,score,in_ism\n")
        for c in range(NUM_CH):
            fh.write(f"{c},{nrf_freq_mhz(c):.0f},{rpd_occ[c]:.4f},{wp[c]:.1f},{score[c]:.4f},{int(c <= ISM_TOP_CH)}\n")
    label = args.label or raw.stem
    plot(rpd_occ, wifi, score, best, plan_chs, base.with_suffix(".png"), label)
    print("wrote", base.with_suffix(".csv"), "and", base.with_suffix(".png"))

    print(f"\nRPD sweeps: {sweeps}   WiFi APs seen: {len(wifi)}")
    by_wch: dict[int, list[int]] = {}
    for _s, _b, wch, rssi in wifi:
        by_wch.setdefault(wch, []).append(rssi)
    for wch in sorted(by_wch):
        r = by_wch[wch]
        print(f"  WiFi ch {wch:2d} ({wifi_center_mhz(wch):.0f} MHz): {len(r)} AP(s), strongest {max(r)} dBm"
              f"  -> nRF ch {int(wifi_center_mhz(wch) - 2400 - WIFI_HALF_BW_MHZ)}..{int(wifi_center_mhz(wch) - 2400 + WIFI_HALF_BW_MHZ)}")

    print("\nQuietest channels (score = 0.5*WiFi overlap + 0.5*RPD occupancy, lower is better):")
    for c in best10:
        print(f"  ch {c:3d}  {nrf_freq_mhz(c):.0f} MHz  score={score[c]:.3f}  rpd={rpd_occ[c]*100:5.1f}%  wifi={wp[c]:8.1f}")
    if guarded_found:
        print(f"\nRECOMMENDED: ch {best} ({nrf_freq_mhz(best):.0f} MHz) — quietest channel whose ±{half} neighbours are also quiet")
    else:
        print(f"\nRECOMMENDED: ch {best} ({nrf_freq_mhz(best):.0f} MHz) — quietest single channel; "
              f"WARNING no channel had ±{half} quiet neighbours, expect adjacent-channel interference")
    print(f"GROUP PLAN ({args.plan} links, ≥{args.guard} ch apart): {plan_chs}")
    if not args.allow_above_ism:
        above = [c for c in range(ISM_TOP_CH + 1, NUM_CH) if score[c] <= score[best]]
        if above:
            print(f"(channels above the ISM band that look quieter, NOT recommended for the product: {above[:8]}...)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
