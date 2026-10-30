"""Radio settings the dashboard may switch: power level and the clean channels from Increment 2.

Channel list
------------
Taken from all nine RPD + WiFi surveys of the lab in Increment 2
(REII_nRF24_Link_Characterisation/results/channel_survey_2026-09-07_* and _2026-09-08_*,
scored by tools/channel_analysis.py). A channel is listed only if, in EVERY survey:

  * its nRF carrier-detect (RPD) occupancy stayed at or below 3 %, and
  * no WiFi access point overlapped it, or only a weak one at the band edge (2-4).

That leaves two clean blocks inside the ISM band (2400-2483.5 MHz, so 2-81 at 2 Mbps):
75-81, the gap above WiFi ch 11, and 2-4, below WiFi ch 1. 77 is the ICD v1 channel and the
default. `worst_rpd_pct` is the highest RPD occupancy any survey measured on that channel.

Re-run the Increment 2 survey before relying on this list in a different room: WiFi ch 13
(legal in South Africa) would cover nRF 61-83 and was not seen in the lab surveys.
"""

from __future__ import annotations

DEFAULT_CHANNEL = 77          # ICD v1 IF10.0
PA_LEVELS = ("LOW", "HIGH")   # what the dashboard offers; MIN / MAX stay available from the command line
PA_DBM = {"MIN": -18, "LOW": -12, "HIGH": -6, "MAX": 0}

CLEAN_CHANNELS = [
    # ch, worst RPD % over 9 surveys, WiFi seen on it, note
    {"ch": 77, "worst_rpd_pct": 1.0, "wifi": False, "note": "ICD default, Increment 2 recommendation"},
    {"ch": 78, "worst_rpd_pct": 0.5, "wifi": False, "note": "quietest in the 75-81 gap"},
    {"ch": 79, "worst_rpd_pct": 1.0, "wifi": False, "note": ""},
    {"ch": 76, "worst_rpd_pct": 0.5, "wifi": False, "note": ""},
    {"ch": 75, "worst_rpd_pct": 0.5, "wifi": False, "note": "WiFi ch 11 edge 2 channels below"},
    {"ch": 80, "worst_rpd_pct": 2.0, "wifi": False, "note": "Increment 2 control channel"},
    {"ch": 81, "worst_rpd_pct": 3.0, "wifi": False, "note": "top of the ISM band at 2 Mbps"},
    {"ch": 2, "worst_rpd_pct": 1.0, "wifi": True, "note": "below WiFi ch 1, weak AP edge in 1 survey"},
    {"ch": 3, "worst_rpd_pct": 2.0, "wifi": True, "note": "below WiFi ch 1, weak AP edge in 1 survey"},
    {"ch": 4, "worst_rpd_pct": 2.0, "wifi": True, "note": "below WiFi ch 1, weak AP edge in 1 survey"},
]
CLEAN_SET = {c["ch"] for c in CLEAN_CHANNELS}

for _c in CLEAN_CHANNELS:
    _c["mhz"] = 2400 + _c["ch"]
