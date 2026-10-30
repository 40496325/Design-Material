"""ICD v1 IF10.0 byte maps - 32-byte frames between the dashboard and the crane.

Table 33  downlink command (dashboard -> crane), magic 0xA5
Table 34  uplink telemetry  (crane -> dashboard, inside the auto-ACK), magic 0x81

Little-endian, fixed 32 bytes. This file is the Python twin of
crane_rig_IDF/main/icd_frames.h and groundstation_PIO/include/icd_frames.h.

    python icd.py        # runs the pack/unpack self-test
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, asdict

FRAME_BYTES = 32
MAGIC_CMD = 0xA5
MAGIC_TLM = 0x81
MSG_PING = 1
MSG_MOVE = 2

DOWN_STRUCT = struct.Struct("<BBBBbbBB24x")          # Table 33
UP_STRUCT = struct.Struct("<BBhhhhHHBBBBB13x")       # Table 34
assert DOWN_STRUCT.size == FRAME_BYTES and UP_STRUCT.size == FRAME_BYTES

# Table 33 byte 3
MODE_FLAGS = {0: "Soft stop", 1: "Reset faults", 2: "Auto", 3: "Zero position", 4: "Hold brake"}
# Table 34 byte 17
FAULT_FLAGS = {0: "VFD overcurrent", 1: "Overload (> 20 kg)", 2: "Brake fault", 3: "Rail limit",
               4: "Magnet fault", 5: "Encoder fault", 6: "VFD fault", 7: "Wireless timeout"}
# Table 34 byte 18
HEALTH_FLAGS = {0: "Wireless link OK", 1: "E-stop pressed", 2: "Auto mode", 3: "Hoist brake clamped",
                4: "VFD enabled"}
MAGNET_STATUS = {0: "Off", 1: "On (holding)", 2: "Fault"}

# Byte layout for the dashboard's byte strip: (first_byte, last_byte, name, type, meaning)
DOWN_FIELDS = [
    (0, 0, "magic", "uint8", "0xA5 command marker"),
    (1, 1, "msg_type", "uint8", "1 = ping, 2 = move"),
    (2, 2, "sequence", "uint8", "0-255, echoed back as seq_ack"),
    (3, 3, "mode_flags", "bits", "soft stop, reset, auto, zero position, hold brake"),
    (4, 4, "trolley_cmd", "int8", "trolley speed -100..+100 %"),
    (5, 5, "hoist_cmd", "int8", "hoist speed -100..+100 % (+ up)"),
    (6, 6, "magnet_state", "uint8", "0 off, 1 on"),
    (7, 7, "segment_idx", "uint8", "anti-sway ramp step"),
    (8, 31, "padding", "24 bytes", "always 0x00"),
]
UP_FIELDS = [
    (0, 0, "magic", "uint8", "0x81 telemetry marker"),
    (1, 1, "seq_ack", "uint8", "sequence of the command before this one"),
    (2, 3, "trolley_pos", "int16", "mm"),
    (4, 5, "hoist_pos", "int16", "mm"),
    (6, 7, "trolley_vel", "int16", "mm/s"),
    (8, 9, "hoist_vel", "int16", "mm/s"),
    (10, 11, "load_cell", "uint16", "kg x 10"),
    (12, 13, "vfd_current", "uint16", "A x 10"),
    (14, 14, "magnet_current", "uint8", "A x 10"),
    (15, 15, "magnet_status", "uint8", "0 off, 1 holding, 2 fault"),
    (16, 16, "error_code", "uint8", "0 OK, else fault id"),
    (17, 17, "fault_flags", "bits", "one bit per fault"),
    (18, 18, "system_health", "bits", "one bit per status"),
    (19, 31, "padding", "13 bytes", "always 0x00"),
]


def _clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, int(v)))


@dataclass
class Command:
    """Table 33 fields the operator controls."""
    msg_type: int = MSG_MOVE
    sequence: int = 0
    soft_stop: bool = False
    reset: bool = False
    auto: bool = False
    zero_pos: bool = False
    hold_brake: bool = False
    trolley_cmd: int = 0       # -100..100 %
    hoist_cmd: int = 0         # -100..100 %, + = up
    magnet_on: bool = False
    segment_idx: int = 0

    @property
    def mode_flags(self) -> int:
        return (int(self.soft_stop) | int(self.reset) << 1 | int(self.auto) << 2
                | int(self.zero_pos) << 3 | int(self.hold_brake) << 4)


def pack_downlink(c: Command) -> bytes:
    return DOWN_STRUCT.pack(MAGIC_CMD, c.msg_type & 0xFF, c.sequence & 0xFF, c.mode_flags,
                            _clamp(c.trolley_cmd, -100, 100), _clamp(c.hoist_cmd, -100, 100),
                            1 if c.magnet_on else 0, c.segment_idx & 0xFF)


def unpack_downlink(b: bytes) -> dict:
    if len(b) != FRAME_BYTES:
        raise ValueError(f"downlink frame must be {FRAME_BYTES} bytes, got {len(b)}")
    magic, mt, seq, flags, tr, ho, mag, seg = DOWN_STRUCT.unpack(b)
    return {"magic": magic, "msg_type": mt, "sequence": seq, "mode_flags": flags,
            "flags": [name for bit, name in MODE_FLAGS.items() if flags >> bit & 1],
            "trolley_cmd": tr, "hoist_cmd": ho, "magnet_state": mag, "segment_idx": seg,
            "padding_ok": not any(b[8:])}


@dataclass
class Telemetry:
    """Table 34 decoded into engineering units."""
    seq_ack: int
    trolley_pos_mm: int
    hoist_pos_mm: int
    trolley_vel_mm_s: int
    hoist_vel_mm_s: int
    load_kg: float
    vfd_current_a: float
    magnet_current_a: float
    magnet_status: int
    error_code: int
    fault_flags: int
    system_health: int
    padding_ok: bool

    @property
    def faults(self) -> list[str]:
        return [n for bit, n in FAULT_FLAGS.items() if self.fault_flags >> bit & 1]

    @property
    def health(self) -> list[str]:
        return [n for bit, n in HEALTH_FLAGS.items() if self.system_health >> bit & 1]

    def to_dict(self) -> dict:
        d = asdict(self)
        d["faults"] = self.faults
        d["health"] = self.health
        d["magnet_status_text"] = MAGNET_STATUS.get(self.magnet_status, f"unknown ({self.magnet_status})")
        return d


def unpack_uplink(b: bytes) -> Telemetry:
    if len(b) != FRAME_BYTES:
        raise ValueError(f"uplink frame must be {FRAME_BYTES} bytes, got {len(b)}")
    (magic, seq_ack, tpos, hpos, tvel, hvel, load, vfd, mag_i, mag_s, err, faults,
     health) = UP_STRUCT.unpack(b)
    if magic != MAGIC_TLM:
        raise ValueError(f"uplink magic 0x{magic:02X}, expected 0x{MAGIC_TLM:02X}")
    return Telemetry(seq_ack, tpos, hpos, tvel, hvel, load / 10, vfd / 10, mag_i / 10, mag_s, err,
                     faults, health, not any(b[19:]))


def pack_uplink(t: Telemetry) -> bytes:
    """Used by the self-test and the bench emulator (the crane packs this in C)."""
    return UP_STRUCT.pack(MAGIC_TLM, t.seq_ack & 0xFF, t.trolley_pos_mm, t.hoist_pos_mm, t.trolley_vel_mm_s,
                          t.hoist_vel_mm_s, round(t.load_kg * 10), round(t.vfd_current_a * 10),
                          round(t.magnet_current_a * 10), t.magnet_status, t.error_code, t.fault_flags,
                          t.system_health)


def field_maps() -> dict:
    keys = ("first", "last", "name", "type", "meaning")
    return {"downlink": [dict(zip(keys, f)) for f in DOWN_FIELDS],
            "uplink": [dict(zip(keys, f)) for f in UP_FIELDS],
            "mode_flags": MODE_FLAGS, "fault_flags": FAULT_FLAGS, "health_flags": HEALTH_FLAGS}


def _self_test() -> None:
    c = Command(sequence=300, soft_stop=True, hold_brake=True, trolley_cmd=-150, hoist_cmd=42,
                magnet_on=True, segment_idx=7)
    raw = pack_downlink(c)
    assert len(raw) == 32 and raw[0] == 0xA5 and raw[2] == 300 % 256
    d = unpack_downlink(raw)
    assert d["mode_flags"] == 0b10001 and d["trolley_cmd"] == -100 and d["hoist_cmd"] == 42
    assert d["magnet_state"] == 1 and d["segment_idx"] == 7 and d["padding_ok"]
    assert raw.hex().upper().startswith("A5022C11") and raw[4] == 0x9C   # -100 = 0x9C

    t = Telemetry(5, -1234, 1600, 1000, -80, 18.5, 4.2, 7.5, 1, 2, 0b10000010, 0b00011001, True)
    up = pack_uplink(t)
    assert len(up) == 32 and up[0] == 0x81
    back = unpack_uplink(up)
    assert back == t, (back, t)
    assert back.faults == ["Overload (> 20 kg)", "Wireless timeout"]
    assert back.health == ["Wireless link OK", "Hoist brake clamped", "VFD enabled"]
    # byte offsets straight from Table 34
    assert int.from_bytes(up[2:4], "little", signed=True) == -1234
    assert int.from_bytes(up[10:12], "little") == 185 and up[14] == 75 and up[17] == 0x82
    print("icd.py self-test: Table 33 and Table 34 pack/unpack OK (32 bytes each)")


if __name__ == "__main__":
    _self_test()
