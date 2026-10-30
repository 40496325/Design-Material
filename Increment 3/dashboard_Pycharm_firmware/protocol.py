#!/usr/bin/env python3
"""
REII Application Protocol — 32-byte pack/unpack.
Aligned with Group 13 Interface Control Document (ICD v1) Tables 33 & 34.

Downlink (Table 33): 32 bytes
  [0]     magic        u8   0xA5
  [1]     msg_type     u8   0x01 = Ping, 0x02 = Move Command
  [2]     sequence     u8   0-255 counter
  [3]     mode_flags   u8   b0=Soft Stop/E-stop, b1=Reset, b2=Auto, b3=Zero Pos, b4=Hold Brake
  [4]     trolley_cmd  i8   -100% to +100%
  [5]     hoist_cmd    i8   -100% to +100% (+Up / -Down)
  [6]     magnet_state u8   0 = OFF, 1 = ON
  [7]     segment_idx  u8   0-255 anti-sway step index
  [8..31] padding      24 bytes (all 0x00)

Uplink (Table 34): 32 bytes via nRF24 Auto-ACK payload
  [0]     magic        u8   0x81
  [1]     seq_ack      u8   echoes command sequence
  [2..3]  trolley_pos  i16  mm (LE)
  [4..5]  hoist_pos    i16  mm (LE)
  [6..7]  trolley_vel  i16  mm/s (LE)
  [8..9]  hoist_vel    i16  mm/s (LE)
  [10..11] load_cell   u16  kg * 10 (LE)
  [12..13] vfd_current u16  A * 10 (LE)
  [14]    magnet_curr  u8   A * 10 (0.1 A steps, 0-25.5 A, J6.8 / AN3)
  [15]    magnet_stat  u8   0 = OFF, 1 = ON (Holding), 2 = Fault
  [16]    error_code   u8   0 = OK, 1..255 = Fault ID
  [17]    fault_flags  u8   bitmask (b0=VFD OC, b1=Overload, b2=Brake, b3=Rail, b4=Mag, b5=Enc, b6=VFD, b7=Timeout)
  [18]    sys_health   u8   bitmask (b0=Link OK, b1=E-stop, b2=Auto, b3=Brake Clamped, b4=VFD En)
  [19..31] padding     13 bytes (all 0x00)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC_DOWNLINK = 0xA5
MAGIC_UPLINK = 0x81
TYPE_PING = 0x01
TYPE_CMD = 0x02
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
    segment_idx: int = 0

    def pack(self) -> bytes:
        buf = bytearray(PAYLOAD_SIZE)
        buf[0] = self.magic & 0xFF
        buf[1] = self.type & 0xFF
        buf[2] = self.seq & 0xFF
        buf[3] = self.mode_flags & 0xFF
        buf[4] = struct.pack("b", max(-128, min(127, self.trolley)))[0]
        buf[5] = struct.pack("b", max(-128, min(127, self.hoist)))[0]
        buf[6] = 1 if self.magnet else 0
        buf[7] = self.segment_idx & 0xFF
        # Bytes 8..31 are already 0x00 padding
        return bytes(buf)

    @staticmethod
    def unpack(data: bytes) -> "DownlinkFrame":
        if len(data) != PAYLOAD_SIZE:
            raise ValueError(f"downlink frame must be {PAYLOAD_SIZE} bytes, got {len(data)}")
        return DownlinkFrame(
            magic=data[0],
            type=data[1],
            seq=data[2],
            mode_flags=data[3],
            trolley=struct.unpack("b", bytes([data[4]]))[0],
            hoist=struct.unpack("b", bytes([data[5]]))[0],
            magnet=data[6],
            segment_idx=data[7],
        )

    @property
    def estop(self) -> bool:
        return bool(self.mode_flags & 0x01)

    @property
    def reset(self) -> bool:
        return bool(self.mode_flags & 0x02)

    @property
    def manual_mode(self) -> bool:
        return not bool(self.mode_flags & 0x04)


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
    magnet_current_A_x10: int
    magnet_status: int
    error_code: int
    fault_flags: int
    system_health: int

    def pack(self) -> bytes:
        buf = bytearray(PAYLOAD_SIZE)
        buf[0] = self.magic & 0xFF
        buf[1] = self.seq_ack & 0xFF
        struct.pack_into("<h", buf, 2, self.trolley_pos_mm)
        struct.pack_into("<h", buf, 4, self.hoist_pos_mm)
        struct.pack_into("<h", buf, 6, self.trolley_vel_mm_s)
        struct.pack_into("<h", buf, 8, self.hoist_vel_mm_s)
        struct.pack_into("<H", buf, 10, self.load_kg_x10)
        struct.pack_into("<H", buf, 12, self.vfd_current_A_x10)
        buf[14] = self.magnet_current_A_x10 & 0xFF
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
            magic=data[0],
            seq_ack=data[1],
            trolley_pos_mm=struct.unpack_from("<h", data, 2)[0],
            hoist_pos_mm=struct.unpack_from("<h", data, 4)[0],
            trolley_vel_mm_s=struct.unpack_from("<h", data, 6)[0],
            hoist_vel_mm_s=struct.unpack_from("<h", data, 8)[0],
            load_kg_x10=struct.unpack_from("<H", data, 10)[0],
            vfd_current_A_x10=struct.unpack_from("<H", data, 12)[0],
            magnet_current_A_x10=data[14],
            magnet_status=data[15],
            error_code=data[16],
            fault_flags=data[17],
            system_health=data[18],
        )

    @property
    def load_kg(self) -> float:
        return self.load_kg_x10 / 10.0

    @property
    def vfd_current_A(self) -> float:
        return self.vfd_current_A_x10 / 10.0

    @property
    def magnet_current_A(self) -> float:
        return self.magnet_current_A_x10 / 10.0

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


def make_downlink(
    seq: int = 0,
    estop: bool = False,
    reset: bool = False,
    manual: bool = True,
    trolley: int = 0,
    hoist: int = 0,
    magnet: int = 0,
    segment_idx: int = 0,
) -> DownlinkFrame:
    flags = 0
    if estop:
        flags |= 0x01
    if reset:
        flags |= 0x02
    if not manual:
        flags |= 0x04  # b2 = Auto Mode
    return DownlinkFrame(
        magic=MAGIC_DOWNLINK,
        type=TYPE_CMD,
        seq=seq,
        mode_flags=flags,
        trolley=trolley,
        hoist=hoist,
        magnet=magnet,
        segment_idx=segment_idx,
    )