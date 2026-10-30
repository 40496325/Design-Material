// =============================================================================
// icd_frames.h - 32-byte over-the-air frames, ICD v1 IF10.0 Tables 33 and 34
// =============================================================================
// Little-endian, packed, exactly 32 bytes each. The same file is used by the
// ground station (groundstation_PIO/include/icd_frames.h) and the crane
// (crane_rig_IDF/main/icd_frames.h) - keep them identical.
// =============================================================================
#pragma once

#include <stdint.h>

#define ICD_FRAME_BYTES 32

// Table 33 byte 0 / Table 34 byte 0
#define ICD_MAGIC_CMD  0xA5   // downlink: dashboard -> crane
#define ICD_MAGIC_TLM  0x81   // uplink:   crane -> dashboard (ACK payload)

// Table 33 byte 1 (msg_type)
#define ICD_MSG_PING 1
#define ICD_MSG_MOVE 2

// Table 33 byte 3 (mode_flags)
#define ICD_MODE_SOFT_STOP  0x01
#define ICD_MODE_RESET      0x02
#define ICD_MODE_AUTO       0x04
#define ICD_MODE_ZERO_POS   0x08
#define ICD_MODE_HOLD_BRAKE 0x10

// Table 34 byte 17 (fault_flags)
#define ICD_FAULT_VFD_OVERCURRENT 0x01
#define ICD_FAULT_OVERLOAD        0x02
#define ICD_FAULT_BRAKE           0x04
#define ICD_FAULT_RAIL_LIMIT      0x08
#define ICD_FAULT_MAGNET          0x10
#define ICD_FAULT_ENCODER         0x20
#define ICD_FAULT_VFD             0x40
#define ICD_FAULT_WIRELESS_TIMEOUT 0x80

// Table 34 byte 18 (system_health)
#define ICD_HEALTH_LINK_OK      0x01
#define ICD_HEALTH_ESTOP        0x02
#define ICD_HEALTH_AUTO_MODE    0x04
#define ICD_HEALTH_BRAKE_CLAMP  0x08
#define ICD_HEALTH_VFD_ENABLED  0x10

typedef struct __attribute__((packed)) {
    uint8_t magic;          // 0      0xA5
    uint8_t msg_type;       // 1      1 = ping, 2 = move
    uint8_t sequence;       // 2      0..255, echoed back as seq_ack
    uint8_t mode_flags;     // 3      ICD_MODE_*
    int8_t  trolley_cmd;    // 4      -100..+100 %
    int8_t  hoist_cmd;      // 5      -100..+100 % (+up)
    uint8_t magnet_state;   // 6      0 off, 1 on
    uint8_t segment_idx;    // 7      anti-sway / path step
    uint8_t padding[24];    // 8..31  0x00
} icd_downlink_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic;          // 0      0x81
    uint8_t  seq_ack;        // 1      sequence of the last command received
    int16_t  trolley_pos;    // 2..3   mm
    int16_t  hoist_pos;      // 4..5   mm
    int16_t  trolley_vel;    // 6..7   mm/s
    int16_t  hoist_vel;      // 8..9   mm/s
    uint16_t load_cell;      // 10..11 kg x 10
    uint16_t vfd_current;    // 12..13 A x 10
    uint8_t  magnet_current; // 14     A x 10
    uint8_t  magnet_status;  // 15     0 off, 1 holding, 2 fault
    uint8_t  error_code;     // 16     0 OK
    uint8_t  fault_flags;    // 17     ICD_FAULT_*
    uint8_t  system_health;  // 18     ICD_HEALTH_*
    uint8_t  padding[13];    // 19..31 0x00
} icd_uplink_t;

#ifdef __cplusplus
static_assert(sizeof(icd_downlink_t) == ICD_FRAME_BYTES, "icd_downlink_t must be 32 bytes");
static_assert(sizeof(icd_uplink_t) == ICD_FRAME_BYTES, "icd_uplink_t must be 32 bytes");
#else
_Static_assert(sizeof(icd_downlink_t) == ICD_FRAME_BYTES, "icd_downlink_t must be 32 bytes");
_Static_assert(sizeof(icd_uplink_t) == ICD_FRAME_BYTES, "icd_uplink_t must be 32 bytes");
#endif
