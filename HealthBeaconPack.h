// RTNode-2400 — Health Beacon payload packer (v1)
//
// Packs the 14-byte, big-endian health-beacon payload carried in the app_data
// of a periodic RNS announce on the `rtnode.health` aspect. This is the wire
// contract shared with the field tool's decoder
// (reticulum-tool: monitor/health_beacon.py, struct ">BIHbBBBBBB").
//
// Kept pure (stdint only, no Arduino/RNS deps) so the exact byte layout can be
// unit-tested off-device against the tool's golden vector. The higher-level
// builder that pulls live values from collect_health() lives in HealthBeacon.h.
//
// Wire layout (big-endian):
//   [0]      format version (0x01)
//   [1..4]   uptime seconds            (uint32)
//   [5..6]   free heap KB              (uint16, low-water/min preferred)
//   [7]      WiFi RSSI dBm             (int8; 0 when WiFi down)
//   [8]      reset reason              (0 poweron,1 panic,2 brownout,3 task_wdt,
//                                       4 sw,5 other)
//   [9]      flags                     (see HB_FLAG_* below)
//   [10]     board id                  (= BOARD_MODEL; 0x3F = Heltec V4)
//   [11..13] firmware version          (major, minor, patch)
//
// Newer formats may APPEND bytes; the first 14 stay stable (decoder tolerates
// trailing bytes).

#ifndef HEALTHBEACONPACK_H
#define HEALTHBEACONPACK_H

#include <stdint.h>
#include <stdbool.h>

#define HEALTH_BEACON_FORMAT_VERSION 0x01
#define HEALTH_BEACON_LEN            14

// On-demand poll request opcode (1-byte packet to the rtnode.health dest).
// Matches reticulum-tool monitor/health_poll.OPCODE_FULL_HEALTH. Unknown
// opcodes are ignored by the firmware so the registry can grow independently.
#define HB_OPCODE_FULL_HEALTH        0x01

// Flag bit positions — MUST match reticulum-tool monitor/health_beacon.py.
enum {
    HB_FLAG_WIFI_UP         = 0x01,  // bit0
    HB_FLAG_LORA_UP         = 0x02,  // bit1
    HB_FLAG_TCP_BACKBONE_UP = 0x04,  // bit2
    HB_FLAG_LOCAL_TCP_UP    = 0x08,  // bit3
    HB_FLAG_WDT_ARMED       = 0x10,  // bit4
    HB_FLAG_PSRAM           = 0x20,  // bit5
    HB_FLAG_FAULT           = 0x40,  // bit6 (confirmed persistent fault)
    HB_FLAG_AIRTIME_LOCK    = 0x80,  // bit7 (duty-cycle limiter engaged)
};

// Reset-reason enum values (match the tool's RESET_REASONS map).
enum {
    HB_RESET_POWERON  = 0,
    HB_RESET_PANIC    = 1,
    HB_RESET_BROWNOUT = 2,
    HB_RESET_TASK_WDT = 3,
    HB_RESET_SW       = 4,
    HB_RESET_OTHER    = 5,
};

// Packs the payload. Bytes are written big-endian explicitly (shift/mask) so
// the output is identical regardless of host endianness.
static inline void health_pack_beacon(
    uint8_t  out[HEALTH_BEACON_LEN],
    uint32_t uptime_s,
    uint16_t heap_kb,
    int8_t   rssi_dbm,
    uint8_t  reset_code,
    bool wifi_up, bool lora_up, bool tcp_backbone_up, bool local_tcp_up,
    bool wdt_armed, bool psram, bool fault, bool airtime_lock,
    uint8_t  board_id,
    uint8_t  fw_major, uint8_t fw_minor, uint8_t fw_patch)
{
    uint8_t flags = 0;
    if (wifi_up)         flags |= HB_FLAG_WIFI_UP;
    if (lora_up)         flags |= HB_FLAG_LORA_UP;
    if (tcp_backbone_up) flags |= HB_FLAG_TCP_BACKBONE_UP;
    if (local_tcp_up)    flags |= HB_FLAG_LOCAL_TCP_UP;
    if (wdt_armed)       flags |= HB_FLAG_WDT_ARMED;
    if (psram)           flags |= HB_FLAG_PSRAM;
    if (fault)           flags |= HB_FLAG_FAULT;
    if (airtime_lock)    flags |= HB_FLAG_AIRTIME_LOCK;

    out[0]  = HEALTH_BEACON_FORMAT_VERSION;
    out[1]  = (uint8_t)(uptime_s >> 24);
    out[2]  = (uint8_t)(uptime_s >> 16);
    out[3]  = (uint8_t)(uptime_s >> 8);
    out[4]  = (uint8_t)(uptime_s);
    out[5]  = (uint8_t)(heap_kb >> 8);
    out[6]  = (uint8_t)(heap_kb);
    out[7]  = (uint8_t)rssi_dbm;      // two's-complement byte for int8
    out[8]  = reset_code;
    out[9]  = flags;
    out[10] = board_id;
    out[11] = fw_major;
    out[12] = fw_minor;
    out[13] = fw_patch;
}

#endif // HEALTHBEACONPACK_H
