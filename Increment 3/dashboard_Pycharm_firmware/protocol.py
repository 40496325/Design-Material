#!/usr/bin/env python3
"""
REII Application Protocol — 32-byte pack/unpack (ICD §4, PROVISIONAL per D-01).

WARNING: The byte maps below are the REII-proposed maps from the D-01 payload
workshop email. They are PROVISIONAL — ICD §4 is TBD/unsigned and no byte map
is frozen. Do not treat these as final. Update when the signed ICD §4 is
published.

Hard constraint: 32 bytes per frame, static length, 5-byte address, CRC-16.
All integers fixed-point (no float on controller).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC_DOWNLINK = 0xA5
MAGIC_UPLINK = 0x81
TYPE_PING = 0x01
TYPE_CMD = 0x02
TYPE_TELEMETRY = 0x03
PAYLOAD_SIZE = 32

@dataclass
class DownlinkFrame:
    magic: int
    type: int
    seq: int
    mode_flags: int
    trolley: int
    hoist: int
    magnet: int

    def pack(self) -> bytes:
        buf = bytearray(PAYLOAD_SIZE)
        buf[0] = self.magic
        buf[1] = self.type
        buf[2] = self.seq & 0xFF
        buf[3] = self.mode_flags & 0xFF
        buf[4] = struct.pack("b", self.trolley)[0]
        buf[5] = struct.pack("b", self.hoist)[0]
        buf[6] = self.magnet & 0xFF
        return bytes(buf)

    @staticmethod
    def unpack(data: bytes) -> "DownlinkFrame":
        if len(data) != PAYLOAD_SIZE:
            raise ValueError(f"downlink frame must be {PAYLOAD_SIZE} bytes, got {len(data)}")
        return DownlinkFrame(
            magic=data[0], type=data[1], seq=data[2], mode_flags=data[3],
            trolley=struct.unpack("b", bytes([data[4]]))[0],
            hoist=struct.unpack("b", bytes([data[5]]))[0],
            magnet=data[6],
        )

    @property
    def estop(self) -> bool:
        return bool(self.mode_flags & 0x01)

    @property
    def reset(self) -> bool:
        return bool(self.mode_flags & 0x02)

    @property
    def manual_mode(self) -> bool:
        return bool(self.mode_flags & 0x04)


# ---------------------------------------------------------------------------
# Uplink (crane -> dashboard via ACK-payload) — 32 bytes
#   [0]  magic       u8   0x81
#   [1]  seq_ack     u8   acknowledged seq
#   [2..3] trolley_pos i16 mm
#   [4..5] hoist_pos   i16 mm
#   [6..7] trolley_vel i16 mm/s
#   [8..9] hoist_vel   i16 mm/s
#   [10..11] load      u16 kg*10
#   [12..13] vfd_cur   u16 A*10
#   [14]   temp        i8  C
#   [15]   magnet_stat u8  0=off, 1=on
#   [16]   error_code  u8
#   [17]   fault_flags u8
#   [18]   sys_health  u8
#   [19..31] reserved (13 bytes)
# ---------------------------------------------------------------------------
@dataclass
class UplinkFrame:
    magic: int
    seq_ack: int
    trolley_pos_mm: int
    hoist_pos_mm: int
    trolley_vel_mm_s: int
    hoist_vel_mm_s: int
    load_kg_x10: int
    vfd_current_A_x10: int
    temp_c: int
    magnet_status: int
    error_code: int
    fault_flags: int
    system_health: int

    def pack(self) -> bytes:
        buf = bytearray(PAYLOAD_SIZE)
        buf[0] = self.magic
        buf[1] = self.seq_ack & 0xFF
        struct.pack_into("<h", buf, 2, self.trolley_pos_mm)
        struct.pack_into("<h", buf, 4, self.hoist_pos_mm)
        struct.pack_into("<h", buf, 6, self.trolley_vel_mm_s)
        struct.pack_into("<h", buf, 8, self.hoist_vel_mm_s)
        struct.pack_into("<H", buf, 10, self.load_kg_x10)
        struct.pack_into("<H", buf, 12, self.vfd_current_A_x10)
        buf[14] = struct.pack("b", self.temp_c)[0]
        buf[15] = self.magnet_status & 0xFF
        buf[16] = self.error_code & 0xFF
        buf[17] = self.fault_flags & 0xFF
        buf[18] = self.system_health & 0xFF
        return bytes(buf)

    @staticmethod
    def unpack(data: bytes) -> "UplinkFrame":
        if len(data) != PAYLOAD_SIZE:
            raise ValueError(f"uplink frame must be {PAYLOAD_SIZE} bytes, got {len(data)}")
        return UplinkFrame(
            magic=data[0], seq_ack=data[1],
            trolley_pos_mm=struct.unpack_from("<h", data, 2)[0],
            hoist_pos_mm=struct.unpack_from("<h", data, 4)[0],
            trolley_vel_mm_s=struct.unpack_from("<h", data, 6)[0],
            hoist_vel_mm_s=struct.unpack_from("<h", data, 8)[0],
            load_kg_x10=struct.unpack_from("<H", data, 10)[0],
            vfd_current_A_x10=struct.unpack_from("<H", data, 12)[0],
            temp_c=struct.unpack("b", bytes([data[14]]))[0],
            magnet_status=data[15], error_code=data[16],
            fault_flags=data[17], system_health=data[18],
        )

    @property
    def load_kg(self) -> float:
        return self.load_kg_x10 / 10.0

    @property
    def vfd_current_A(self) -> float:
        return self.vfd_current_A_x10 / 10.0

    @property
    def trolley_pos_m(self) -> float:
        return self.trolley_pos_mm / 1000.0

    @property
    def hoist_pos_m(self) -> float:
        return self.hoist_pos_mm / 1000.0

    @property
    def trolley_vel_m_s(self) -> float:
        return self.trolley_vel_mm_s / 1000.0

    @property
    def hoist_vel_m_s(self) -> float:
        return self.hoist_vel_mm_s / 1000.0


def make_downlink(seq: int = 0, estop: bool = False, reset: bool = False,
                  manual: bool = True, trolley: int = 0, hoist: int = 0,
                  magnet: int = 0) -> DownlinkFrame:
    flags = 0
    if estop:
        flags |= 0x01
    if reset:
        flags |= 0x02
    if manual:
        flags |= 0x04
    return DownlinkFrame(
        magic=MAGIC_DOWNLINK, type=TYPE_CMD, seq=seq,
        mode_flags=flags, trolley=trolley, hoist=hoist, magnet=magnet,
    )


def make_uplink(seq_ack: int = 0) -> UplinkFrame:
    return UplinkFrame(
        magic=MAGIC_UPLINK, seq_ack=seq_ack,
        trolley_pos_mm=0, hoist_pos_mm=0,
        trolley_vel_mm_s=0, hoist_vel_mm_s=0,
        load_kg_x10=0, vfd_current_A_x10=0,
        temp_c=0, magnet_status=0, error_code=0,
        fault_flags=0, system_health=0,
    )


assert struct.calcsize("<BBBBbbB") <= PAYLOAD_SIZE, "downlink header too large"
assert struct.calcsize("<BBhhhhHHbbbbb") <= PAYLOAD_SIZE, "uplink header too large"