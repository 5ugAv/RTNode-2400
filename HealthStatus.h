// RTNode-2400 — Health Status
//
// Exposes board health data for the field diagnostic/repair tool
// ("an OBD scanner for Reticulum nodes"). Two consumers share this layer:
//
//   Phase 1 (this file): a JSON `GET /status` endpoint served over the LAN
//           while the node is in WiFi station mode.
//   Phase 2 (future):    an LXMF health beacon pushed over the Reticulum mesh.
//           It reuses collect_health() so both paths report identical data.
//
// Design notes:
//   * No ArduinoJson dependency — the payload is small and fixed, so a
//     hand-rolled String keeps the firmware footprint down.
//   * Data is read from existing globals (firewall_state, radio_online,
//     WiFi.*, heap_caps_*). The only firmware-local dependency is
//     health_local_server_up(), defined in RNode_Firmware.ino where the
//     TcpInterface pointers are visible.
//   * The status server binds port 80. In normal (station-mode) operation
//     nothing else listens there — the Console and config-portal web servers
//     only begin() in soft-AP mode, which is mutually exclusive with this.

#ifndef HEALTHSTATUS_H
#define HEALTHSTATUS_H

#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

#include "FirewallMode.h"   // FirewallState / firewall_state

// Fork version string. NOTE: this is the single in-firmware source of truth
// for the RTNode fork version (upstream RNode's MAJ_VERS/MIN_VERS is a
// separate protocol version). Bump it in lockstep with release tags.
#ifndef RTNODE_FORK_VERSION
#define RTNODE_FORK_VERSION "0.6.2"
#endif

#define RTNODE_HEALTH_PORT 80

// radio_online lives in Config.h; extern-declared here so this header does
// not depend on include ordering.
extern bool radio_online;

// Runtime status of the local (LAN) TCP server. Defined in the main sketch,
// where local_tcp_interface_ptr and the TcpInterface type are in scope.
extern bool health_local_server_up();

// ─── Snapshot ───────────────────────────────────────────────────────────────
// Plain data, populated by collect_health(). Kept serialization-agnostic so
// the Phase 2 LXMF beacon can encode it however it needs.
struct HealthSnapshot {
    const char* fork;
    const char* fw_version;
    uint8_t     rnode_maj;
    uint8_t     rnode_min;

    uint16_t    board_model;
    const char* board_name;
    bool        psram;
    uint32_t    psram_size;

    uint32_t    uptime_ms;
    const char* reset_reason;

    uint32_t    heap_internal_free;   // internal SRAM — what WiFi RX buffers need
    uint32_t    heap_internal_min;    // low-water mark since boot
    uint32_t    heap_total_free;      // internal + PSRAM

    bool        wdt_armed;
    uint16_t    wdt_timeout_s;

    bool        wifi_connected;
    int32_t     wifi_rssi;
    IPAddress   wifi_ip;

    bool        lora_online;
    bool        tcp_backbone_connected;
    bool        local_tcp_server_up;
    bool        local_tcp_client_connected;

    const char* node_name;
};

inline const char* health_board_name() {
#if BOARD_MODEL == BOARD_HELTEC32_V4
    return "heltec_v4";
#elif BOARD_MODEL == BOARD_HELTEC32_V3
    return "heltec_v3";
#else
    return "unknown";
#endif
}

inline const char* health_reset_reason_str() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";        // crash / abort()
        case ESP_RST_INT_WDT:   return "int_wdt";      // interrupt watchdog
        case ESP_RST_TASK_WDT:  return "task_wdt";     // task watchdog timeout
        case ESP_RST_WDT:       return "other_wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";     // undervoltage
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

// ─── Collection ─────────────────────────────────────────────────────────────
// Reads current health from existing globals. Cheap — safe to call per beacon
// or per HTTP request.
inline void collect_health(HealthSnapshot& h) {
    h.fork       = "RTNode";
    h.fw_version = RTNODE_FORK_VERSION;
#if defined(MAJ_VERS) && defined(MIN_VERS)
    h.rnode_maj  = MAJ_VERS;
    h.rnode_min  = MIN_VERS;
#else
    h.rnode_maj  = 0;
    h.rnode_min  = 0;
#endif

    h.board_model = BOARD_MODEL;
    h.board_name  = health_board_name();
    h.psram_size  = ESP.getPsramSize();
    h.psram       = (h.psram_size > 0);

    h.uptime_ms    = millis();
    h.reset_reason = health_reset_reason_str();

    h.heap_internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    h.heap_internal_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    h.heap_total_free    = ESP.getFreeHeap();

    // The task watchdog is armed unconditionally at boot on ESP32
    // (esp_task_wdt_init(WDT_TIMEOUT, true)), so this is a static "yes".
    h.wdt_armed     = true;
#ifdef WDT_TIMEOUT
    h.wdt_timeout_s = WDT_TIMEOUT;
#else
    h.wdt_timeout_s = 0;
#endif

    h.wifi_connected = (WiFi.status() == WL_CONNECTED);
    h.wifi_rssi      = h.wifi_connected ? WiFi.RSSI() : 0;
    h.wifi_ip        = WiFi.localIP();

    h.lora_online                 = radio_online;
    h.tcp_backbone_connected      = firewall_state.tcp_connected;
    h.local_tcp_server_up         = health_local_server_up();
    h.local_tcp_client_connected  = firewall_state.ap_tcp_connected;

    h.node_name = firewall_state.node_name;
}

// ─── JSON serialization ─────────────────────────────────────────────────────
inline String health_to_json(const HealthSnapshot& h) {
    String j;
    j.reserve(640);
    j += "{";
    j += "\"fork\":\"";        j += h.fork;        j += "\",";
    j += "\"fw_version\":\"";  j += h.fw_version;  j += "\",";
    j += "\"rnode_proto\":\""; j += h.rnode_maj; j += "."; j += h.rnode_min; j += "\",";

    j += "\"board_model\":"; j += h.board_model; j += ",";
    j += "\"board\":\"";     j += h.board_name;  j += "\",";
    j += "\"psram\":";       j += (h.psram ? "true" : "false"); j += ",";
    j += "\"psram_size\":";  j += h.psram_size;  j += ",";

    j += "\"uptime_ms\":";    j += h.uptime_ms;    j += ",";
    j += "\"reset_reason\":\""; j += h.reset_reason; j += "\",";

    j += "\"heap_internal_free\":"; j += h.heap_internal_free; j += ",";
    j += "\"heap_internal_min\":";  j += h.heap_internal_min;  j += ",";
    j += "\"heap_total_free\":";    j += h.heap_total_free;    j += ",";

    j += "\"wdt_armed\":";     j += (h.wdt_armed ? "true" : "false"); j += ",";
    j += "\"wdt_timeout_s\":"; j += h.wdt_timeout_s; j += ",";

    j += "\"wifi_connected\":"; j += (h.wifi_connected ? "true" : "false"); j += ",";
    j += "\"wifi_rssi\":";      j += h.wifi_rssi; j += ",";
    j += "\"wifi_ip\":\"";      j += h.wifi_ip.toString(); j += "\",";

    j += "\"lora_online\":";                j += (h.lora_online ? "true" : "false"); j += ",";
    j += "\"tcp_backbone_connected\":";     j += (h.tcp_backbone_connected ? "true" : "false"); j += ",";
    j += "\"local_tcp_server_up\":";        j += (h.local_tcp_server_up ? "true" : "false"); j += ",";
    j += "\"local_tcp_client_connected\":"; j += (h.local_tcp_client_connected ? "true" : "false"); j += ",";

    j += "\"node_name\":\""; j += (h.node_name ? h.node_name : ""); j += "\",";

    // Reserved for Phase 2: boot-log FATAL/ERROR capture. Emitted now so the
    // Pi tool can rely on a stable schema.
    j += "\"faults\":[]";
    j += "}";
    return j;
}

// ─── STA-mode status web server ─────────────────────────────────────────────
static WebServer* health_server        = nullptr;
static bool       health_server_started = false;

inline void health_handle_status() {
    HealthSnapshot h;
    collect_health(h);
    health_server->sendHeader("Cache-Control", "no-store");
    health_server->send(200, "application/json", health_to_json(h));
    // Visible "someone is talking to me" confirmation on the RGB LED, after
    // the response is sent so it never adds latency to the data itself.
    led_indicate_activity();
}

// Idempotent: starts the server the first time WiFi station mode is up, and
// no-ops thereafter. Safe to call every loop iteration.
inline void health_server_ensure_started() {
    if (health_server_started) return;
    if (!(firewall_state.wifi_enabled && WiFi.status() == WL_CONNECTED)) return;

    if (health_server == nullptr) {
        health_server = new WebServer(RTNODE_HEALTH_PORT);
        health_server->on("/status", HTTP_GET, health_handle_status);
    }
    health_server->begin();
    health_server_started = true;
    Serial.printf("[Health] Status endpoint up: http://%s/status\r\n",
                  WiFi.localIP().toString().c_str());
}

inline void health_server_loop() {
    if (health_server_started && health_server != nullptr) {
        health_server->handleClient();
    }
}

#endif // HEALTHSTATUS_H
