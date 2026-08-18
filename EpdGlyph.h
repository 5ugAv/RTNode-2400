// Copyright (C) 2026
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// EpdGlyph.h — monochrome status-glyph state machine + renderer for the
// LilyGO T-Echo's 1.54" black-and-white e-paper (GxEPD2_154_D67).
//
// ARCHITECTURE (matches the brief): this file has NO Arduino, GxEPD2, or RNS
// dependencies — only <stdint.h>, <string.h> and <math.h> (trig for arcs).
// It compiles standalone with plain g++, the same way HealthBeaconPack.h
// does for the firmware-vs-python wire-contract test (see
// reticulum-tool/tests/test_firmware_beacon_contract.py).
//
// ============================================================================
// REVISION NOTE — this is EVENT-DRIVEN, not the frame-clocked animation the
// original brief specced. An adversarial review (verified independently
// against this repo's own files before this rewrite) found the original
// design physically cannot run on this hardware:
//
//  1. GxEPD2_154_D67.h: partial_refresh_time=500ms, full_refresh_time=2600ms,
//     and GxEPD2's refresh() calls _waitWhileBusy() — SYNCHRONOUS, blocks the
//     whole main loop for that long. The brief's "500ms minimum frame
//     interval" leaves *zero* slack: a perpetual 500ms-cadence loop would
//     mean the panel is *always* mid-refresh. Config.h STATUS_INTERVAL_MS=3
//     confirms the main loop samples radio/DCD status every 3ms — a
//     perpetual 500ms-blocking redraw loop starves that by ~166x while it
//     runs.
//  2. E-paper partial refreshes carry a hard, physical wear budget (rated
//     cycle count; manufacturer guidance favours refresh intervals in the
//     minutes and a full refresh every handful of partials, not every 50).
//     STANDBY specced as "one partial every 3s, forever, while idle" would
//     burn through a large fraction of the panel's entire rated life in
//     roughly a month of the device sitting on a shelf, before it ever
//     transmits a packet. This is the disqualifying finding: it is not a
//     performance nit, it is "the screen wears out."
//
// So the mechanism changed: the renderer draws ONCE on a genuine state
// TRANSITION (an event that already happens at real-world, not synthetic,
// cadence) and then leaves the panel alone until the next transition.
// STANDBY / NOT_READY / CONSOLE / FAULT / INTERFERENCE are fully static —
// drawn once on entry, zero redraws while the condition holds. TRANSMITTING
// / RECEIVING / BEACON still get a short (3-frame) animated burst, because
// those correspond to real, naturally rate-limited radio events (a packet
// going out), not a synthetic clock — but the burst is BOUNDED: it plays
// once and then holds its last frame statically, it does not loop for the
// duration of an ongoing transmission. INTERFERENCE is additionally
// debounced to a several-second dwell time, because the firmware's own
// interference filter can settle/re-trigger in milliseconds (confirmed:
// RNode_Firmware.ino's update_dcd() recomputes interference_detected every
// modem-status tick) and redrawing on every flap would be exactly the
// perpetual-redraw problem again.
//
// The priority table, the level/event split, and "screen and indicator can
// never disagree" property are UNCHANGED from the original design — only
// "when do we touch the panel" changed.
// ============================================================================
//
// REFERENCE ARCHITECTURE DEVIATION (separate from the above — read before
// editing set_tx/set_rx call sites in Display.h / RNode_Firmware.ino):
//
// heltec-tracker-rnode/Display.h derives its animation mode from the
// NeoPixel's CURRENT COLOUR (tb_mode_for(r,g,b)), because on that board
// every high-level LED state sets a distinct, unambiguous RGB triple. The
// T-Echo has no NeoPixel — Utilities.h's led_rx_on()/led_tx_on() for
// BOARD_TECHO just toggle two plain GPIO pins, and those SAME low-level
// calls are reused as generic blink primitives by led_indicate_standby(),
// led_indicate_not_ready(), led_indicate_error(), etc. (confirmed by reading
// Utilities.h: led_indicate_standby() calls led_rx_on()/led_rx_off() itself,
// on the same pin real RX uses; led_indicate_console() just calls
// led_indicate_standby(); and led_indicate_airtime_lock() /
// led_indicate_probe() / led_indicate_activity() are `#if HAS_NP == true`
// with NO #else — i.e. empty, no-op functions on this board today. There is
// no existing T-Echo LED vocabulary for interference/probe/activity to
// "keep identical" — that part of the brief assumed a vocabulary this board
// does not have. TX and RX ARE cleanly separable at their own call sites
// (display_tx latch, dcd_led), which is what this module actually keys on.
// Hooking glyph state directly inside led_rx_on()/led_tx_on() would make
// the STANDBY/NOT_READY blink primitives misfire as RECEIVING, so instead
// this module is fed from the firmware's own unambiguous ground-truth
// globals (hw_ready, console_active, interference_detected, airtime_lock,
// radio_error, dcd_led, display_tx — all declared in Config.h, parsed
// before Display.h) at one dedicated call site in Display.h.

#ifndef EPD_GLYPH_H_INCLUDED
#define EPD_GLYPH_H_INCLUDED

#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef EPD_GLYPH_PI
#define EPD_GLYPH_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
#define EPD_GLYPH_W 64
#define EPD_GLYPH_H 64
#define EPD_GLYPH_CX 32
#define EPD_GLYPH_CY 32

// Universal anchor dot. Spec: "filled ~8px dot", read as diameter -> r=4.
#define EPD_GLYPH_DOT_R 4

// Defensive floor, kept at the brief's original 500ms — but its job
// changed. It no longer needs to cap a perpetual clocked loop (there isn't
// one any more: every level state renders once on entry and then goes
// inert, see frame_for() below), so it does not carry the panel-wear risk
// the brief's literal use of this number did. Its only remaining job is
// (a) letting a bounded TX/RX/BEACON burst's own internal frames (spaced
// EPD_GLYPH_BURST_FRAME_MS = 500ms apart) actually get sampled — an
// earlier draft of this file raised this floor to 2000ms out of extra
// caution and it silently broke that: tick() could no longer land on the
// 500ms/1000ms frame boundaries, so bursts jumped straight from frame A to
// frame C and frame B was never shown (caught by
// test_sustained_tx_is_bounded, which expected <=3 redraws for an unbroken
// TX and got exactly 2) — and (b) capping pathological STATE flapping
// (e.g. a radio bug toggling TX on/off every few ms) from generating a
// fresh bounded burst on every flap. 500ms bounds that fine.
#define EPD_GLYPH_MIN_INTERVAL_MS 500u

#define EPD_GLYPH_STATIC_MS 0xFFFFFFFFu

// Interference must be a confirmed, human-timescale condition before it
// ever reaches the panel — the raw firmware flag can flap in milliseconds.
#define EPD_GLYPH_INTERFERENCE_DEBOUNCE_MS 3000u

// Ghosting-clear schedule. Tightened from the brief's "every 50 partials"
// to "every 5" on the coordinator's manufacturer-guidance review — partials
// are now rare (event-driven) so this is cheap, and erring toward the
// panel's wear budget costs nothing in practice.
#define EPD_GLYPH_FULL_REFRESH_PARTIAL_COUNT 5u
#define EPD_GLYPH_FULL_REFRESH_MS            (10u * 60u * 1000u)

// Interference speckle field: single static badge, ~15% density, clear
// zone around the dot.
#define EPD_GLYPH_SPECKLE_DENSITY 0.15f
#define EPD_GLYPH_SPECKLE_CLEAR_R 10

// TRANSMITTING/RECEIVING/BEACON: bounded 3-step burst (A/B/C), holds C.
#define EPD_GLYPH_BURST_FRAME_MS 500u
// Beacon holds C for an extra second before its window lapses.
#define EPD_GLYPH_BEACON_HOLD_MS 1000u
#define EPD_GLYPH_BEACON_TOTAL_MS (EPD_GLYPH_BURST_FRAME_MS * 2u + EPD_GLYPH_BEACON_HOLD_MS)

// ---------------------------------------------------------------------------
// Bitmap: 1 byte per pixel (0/1) for test/debug clarity. Real firmware
// blitting packs this to GxEPD2's buffer at the integration site
// (Display.h), not here — this module never touches hardware.
// ---------------------------------------------------------------------------
struct EpdGlyphBitmap {
    uint8_t px[EPD_GLYPH_H][EPD_GLYPH_W];

    void clear() { memset(px, 0, sizeof(px)); }

    void set(int x, int y) {
        if (x >= 0 && x < EPD_GLYPH_W && y >= 0 && y < EPD_GLYPH_H) px[y][x] = 1;
    }

    bool get(int x, int y) const {
        if (x < 0 || x >= EPD_GLYPH_W || y < 0 || y >= EPD_GLYPH_H) return false;
        return px[y][x] != 0;
    }

    int count_set() const {
        int n = 0;
        for (int y = 0; y < EPD_GLYPH_H; y++)
            for (int x = 0; x < EPD_GLYPH_W; x++)
                if (px[y][x]) n++;
        return n;
    }

    bool equals(const EpdGlyphBitmap &o) const {
        return memcmp(px, o.px, sizeof(px)) == 0;
    }
};

// ---------------------------------------------------------------------------
// Draw primitives — tiny, dependency-free ("tiny drawing routines, no
// runtime font rendering" half of the brief).
// ---------------------------------------------------------------------------
namespace epd_glyph_draw {

inline void fill_circle(EpdGlyphBitmap &b, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) b.set(cx + x, cy + y);
}

// Stamps a filled 2x2-ish dot at (x,y) so 1px-radius arcs stay visible on a
// 1-bit panel (a true 1px-wide circle at small radii is barely there).
inline void stamp(EpdGlyphBitmap &b, int x, int y) {
    b.set(x, y);
    b.set(x + 1, y);
    b.set(x, y + 1);
}

inline void line(EpdGlyphBitmap &b, int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        b.set(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

// Draws an arc of `r` pixels radius spanning [deg_start, deg_end] (degrees,
// 0 = +x axis/right, 90 = +y axis/down — screen coordinates), stamped thick
// enough (2px) to read on a 1-bit panel. NOTE (flagged, unproven): at r=14
// a 60-degree arc is only ~14px of no-antialiasing 1-bit pixels — visually
// this will read as a short jagged stub, not a smooth arc. That is a
// property of the panel (1-bit, no AA) and the small radius, not a bug in
// this function; it cannot be fixed without either a bigger radius or an
// entirely different glyph vocabulary. Flagged for the operator to judge on
// real hardware — compile-only testing cannot evaluate legibility.
inline void arc(EpdGlyphBitmap &b, int cx, int cy, int r, float deg_start, float deg_end) {
    float span = deg_end - deg_start;
    int steps = (int)(fabsf(span) / 3.0f) + 2; // ~3 degree resolution
    for (int i = 0; i <= steps; i++) {
        float deg = deg_start + span * ((float)i / (float)steps);
        float rad = deg * (float)(EPD_GLYPH_PI / 180.0);
        int x = cx + (int)lroundf(r * cosf(rad));
        int y = cy + (int)lroundf(r * sinf(rad));
        stamp(b, x, y);
    }
}

// Deterministic xorshift32 — same seed always yields the same speckle
// pattern, which is what makes the speckle test reproducible.
struct Xorshift32 {
    uint32_t s;
    explicit Xorshift32(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
};

// Places exactly `n` speckles at random points at least `clear_r` px from
// (clear_cx, clear_cy). An exact count (rather than a per-pixel coin flip)
// gives a hard upper bound on density instead of a statistical one.
inline void speckle(EpdGlyphBitmap &b, int n, int clear_cx, int clear_cy, int clear_r, uint32_t seed) {
    Xorshift32 rng(seed);
    int placed = 0;
    int attempts = 0;
    int max_attempts = n * 40 + 200;
    while (placed < n && attempts < max_attempts) {
        attempts++;
        int x = (int)(rng.next() % EPD_GLYPH_W);
        int y = (int)(rng.next() % EPD_GLYPH_H);
        int dx = x - clear_cx, dy = y - clear_cy;
        if (dx * dx + dy * dy < clear_r * clear_r) continue;
        if (b.get(x, y)) continue;
        b.set(x, y);
        placed++;
    }
}

} // namespace epd_glyph_draw

// ---------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------
enum EpdGlyphState : uint8_t {
    EPD_GLYPH_STANDBY = 0,
    EPD_GLYPH_TRANSMITTING,
    EPD_GLYPH_RECEIVING,
    EPD_GLYPH_BEACON,
    EPD_GLYPH_FAULT,
    EPD_GLYPH_INTERFERENCE,
    EPD_GLYPH_NOT_READY,
    EPD_GLYPH_CONSOLE,
    EPD_GLYPH_STATE_COUNT
};

inline const char *epd_glyph_state_name(EpdGlyphState s) {
    switch (s) {
        case EPD_GLYPH_STANDBY:       return "STANDBY";
        case EPD_GLYPH_TRANSMITTING:  return "TRANSMITTING";
        case EPD_GLYPH_RECEIVING:     return "RECEIVING";
        case EPD_GLYPH_BEACON:        return "BEACON";
        case EPD_GLYPH_FAULT:         return "FAULT";
        case EPD_GLYPH_INTERFERENCE:  return "INTERFERENCE";
        case EPD_GLYPH_NOT_READY:     return "NOT_READY";
        case EPD_GLYPH_CONSOLE:       return "CONSOLE";
        default:                     return "?";
    }
}

// One-word legend labels: "Idle, Send, Receive, Home, Fault, Noise, Wait,
// Console."
inline const char *epd_glyph_legend_label(EpdGlyphState s) {
    switch (s) {
        case EPD_GLYPH_STANDBY:       return "Idle";
        case EPD_GLYPH_TRANSMITTING:  return "Send";
        case EPD_GLYPH_RECEIVING:     return "Receive";
        case EPD_GLYPH_BEACON:        return "Home";
        case EPD_GLYPH_FAULT:         return "Fault";
        case EPD_GLYPH_INTERFERENCE:  return "Noise";
        case EPD_GLYPH_NOT_READY:     return "Wait";
        case EPD_GLYPH_CONSOLE:       return "Console";
        default:                     return "?";
    }
}

// ---------------------------------------------------------------------------
// Renderer — pure function of (state, frame, seed). Completely separate
// from EpdGlyphMachine below: it knows nothing about time, priority,
// debounce or conditions, only "draw me frame N of state S". A future
// display swaps this one function out and keeps the whole state machine.
// ---------------------------------------------------------------------------
inline void epd_glyph_render(EpdGlyphBitmap &b, EpdGlyphState state, uint8_t frame, uint32_t seed) {
    using namespace epd_glyph_draw;
    b.clear();

    switch (state) {

    case EPD_GLYPH_STANDBY: {
        // Static — drawn once on entry, never redrawn while idle (see
        // revision note: a perpetual breathing loop is a panel-wear
        // hazard). The dot is the only content.
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        break;
    }

    case EPD_GLYPH_TRANSMITTING: {
        // Outward burst: dot + 60-degree arc pairs radiating out. Bounded
        // 3-step sequence (A: r=14. B: r=14,22. C: r=14,22,30) that plays
        // once per TX engagement and holds at C — see EpdGlyphMachine::
        // frame_for(), this is not a perpetual loop.
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        static const int radii[3] = {14, 22, 30};
        int n = frame + 1; // A=1 ring, B=2, C=3
        for (int i = 0; i < n; i++) {
            int r = radii[i];
            arc(b, EPD_GLYPH_CX, EPD_GLYPH_CY, r, -30.0f, 30.0f);     // right, outward-facing
            arc(b, EPD_GLYPH_CX, EPD_GLYPH_CY, r, 150.0f, 210.0f);    // left, outward-facing
        }
        break;
    }

    case EPD_GLYPH_RECEIVING: {
        // Mirror of TX: converge inward. A: r=30. B: r=30,22. C: r=30,22,14.
        // Same bounded-burst-then-hold shape as TRANSMITTING.
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        static const int radii[3] = {30, 22, 14};
        int n = frame + 1;
        for (int i = 0; i < n; i++) {
            int r = radii[i];
            arc(b, EPD_GLYPH_CX, EPD_GLYPH_CY, r, -30.0f, 30.0f);
            arc(b, EPD_GLYPH_CX, EPD_GLYPH_CY, r, 150.0f, 210.0f);
        }
        break;
    }

    case EPD_GLYPH_BEACON: {
        // dot on the left third, house outline on the right third, one
        // travelling pulse between them. Bounded one-shot, same shape as
        // TX/RX.
        int dot_x = 16, dot_y = EPD_GLYPH_CY;
        int house_x = 47, house_y = EPD_GLYPH_CY;
        fill_circle(b, dot_x, dot_y, EPD_GLYPH_DOT_R);

        int hb_left = house_x - 5, hb_right = house_x + 5;
        int hb_top = house_y - 3, hb_bottom = house_y + 5;
        line(b, hb_left, hb_top, hb_left, hb_bottom);
        line(b, hb_right, hb_top, hb_right, hb_bottom);
        line(b, hb_left, hb_bottom, hb_right, hb_bottom);
        int roof_apex_y = hb_top - 6;
        line(b, hb_left - 1, hb_top, house_x, roof_apex_y);
        line(b, hb_right + 1, hb_top, house_x, roof_apex_y);

        int path_start = dot_x + EPD_GLYPH_DOT_R + 2;
        int path_end = hb_left - 2;
        int px;
        if (frame == 0) px = path_start;
        else if (frame == 1) px = (path_start + path_end) / 2;
        else px = path_end;
        fill_circle(b, px, EPD_GLYPH_CY, 2);
        break;
    }

    case EPD_GLYPH_FAULT: {
        // Static — drawn once when the fault is confirmed (or, for an
        // unrecoverable boot_error, once right before the firmware halts
        // forever — see the integration notes in Display.h). Dot persists
        // but struck through.
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        line(b, EPD_GLYPH_CX - 10, EPD_GLYPH_CY + 10, EPD_GLYPH_CX + 10, EPD_GLYPH_CY - 10);

        int apex_x = EPD_GLYPH_CX, apex_y = 8;
        int base_y = apex_y + 28;
        int half_base = (int)lroundf(28.0f / sqrtf(3.0f));
        int base_l = apex_x - half_base, base_r = apex_x + half_base;
        line(b, apex_x, apex_y, base_l, base_y);
        line(b, apex_x, apex_y, base_r, base_y);
        line(b, base_l, base_y, base_r, base_y);

        int ex = apex_x;
        line(b, ex, apex_y + 8, ex, base_y - 7);
        b.set(ex, base_y - 4);
        b.set(ex - 1, base_y - 4);
        b.set(ex, base_y - 3);
        b.set(ex - 1, base_y - 3);
        break;
    }

    case EPD_GLYPH_INTERFERENCE: {
        // Static single badge — drawn once when the (debounced) condition
        // is confirmed, not re-seeded periodically. See revision note:
        // the brief's "two frames looping at 700ms" would re-randomize the
        // speckle field forever while interference persists, which is the
        // same wear hazard as the STANDBY breathe. `frame` is always 0 for
        // this state (see EpdGlyphMachine::frame_for) but the renderer
        // still accepts it as a parameter for testability.
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        int outside = 0;
        for (int y = 0; y < EPD_GLYPH_H; y++)
            for (int x = 0; x < EPD_GLYPH_W; x++) {
                int dx = x - EPD_GLYPH_CX, dy = y - EPD_GLYPH_CY;
                if (dx * dx + dy * dy >= EPD_GLYPH_SPECKLE_CLEAR_R * EPD_GLYPH_SPECKLE_CLEAR_R) outside++;
            }
        int n = (int)(EPD_GLYPH_SPECKLE_DENSITY * outside);
        uint32_t frame_seed = seed ^ (0xA5A5A5A5u + frame * 0x1000193u);
        speckle(b, n, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_SPECKLE_CLEAR_R, frame_seed);
        break;
    }

    case EPD_GLYPH_NOT_READY: {
        // Static — drawn once on entry. The brief's perpetual spinner loop
        // is dropped (revision note): a board that stays not-ready for an
        // extended period must not redraw forever. `frame` is accepted for
        // testability but EpdGlyphMachine always holds this at 0.
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        arc(b, EPD_GLYPH_CX, EPD_GLYPH_CY, 20, 0.0f, 90.0f);
        (void)frame;
        break;
    }

    case EPD_GLYPH_CONSOLE: {
        // Static, unchanged from the original design (it was already a
        // one-shot draw).
        fill_circle(b, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
        int gx = EPD_GLYPH_CX + 10, gy = EPD_GLYPH_CY - 6;
        line(b, gx, gy, gx + 6, gy + 6);
        line(b, gx, gy + 12, gx + 6, gy + 6);
        line(b, gx + 8, gy + 12, gx + 14, gy + 12);
        break;
    }

    default: break;
    }
}

// Half-size (32x32) legend cell for a given state — always its first/static
// frame, since the legend is a static strip. Used only on first boot / long
// press per the brief ("Never otherwise").
inline void epd_glyph_render_legend_cell(EpdGlyphBitmap &half /* caller uses only [0..31] */, EpdGlyphState state) {
    EpdGlyphBitmap full;
    epd_glyph_render(full, state, 0, 0xC0FFEEu);
    half.clear();
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (full.get(x * 2, y * 2) || full.get(x * 2 + 1, y * 2) ||
                full.get(x * 2, y * 2 + 1) || full.get(x * 2 + 1, y * 2 + 1))
                half.set(x, y);
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
// Priority, highest first (unchanged from the original brief):
//   FAULT > NOT_READY > CONSOLE > INTERFERENCE > TRANSMITTING > RECEIVING >
//   BEACON > STANDBY
//
// FAULT / NOT_READY / CONSOLE / TRANSMITTING / RECEIVING are level-
// triggered (set_*), matching the firmware's own always-current booleans.
// INTERFERENCE is level-triggered but debounced (see interference_confirmed
// ()). BEACON is edge-triggered (trigger_beacon): a single "sending home"
// event that plays out over a fixed ~2s window and then lapses on its own.
//
// "Event states interrupt STANDBY, play, and return to the previous state":
// this falls out for free — resolve() is a pure function of the current
// conditions recomputed every tick, so when a higher-priority level clears,
// the resolver naturally lands back on whichever other condition is still
// true (or STANDBY). No explicit "previous state" stack is kept.
//
// REVISION: redraws are now event-driven, not clock-driven. tick() only
// repaints when (a) the resolved state actually changes, or (b) a bounded
// TX/RX/BEACON burst is mid-sequence and its next frame's time has come.
// STANDBY/NOT_READY/CONSOLE/FAULT/INTERFERENCE never self-advance — once
// drawn, they are inert until resolve() picks something else.
class EpdGlyphMachine {
public:
    EpdGlyphMachine() { reset(); }

    void reset() {
        fault_ = not_ready_ = console_ = tx_ = rx_ = false;
        interference_raw_ = false;
        interference_raw_since_ = 0;
        beacon_until_ = 0;
        beacon_started_ms_ = 0;
        current_state_ = EPD_GLYPH_STANDBY;
        frame_index_ = 0;
        state_entered_ms_ = 0;
        last_redraw_ms_ = 0;
        ticked_once_ = false;
        partial_count_ = 0;
        last_full_refresh_ms_ = 0;
        bitmap_.clear();
    }

    void set_fault(bool v)     { fault_ = v; }
    void set_not_ready(bool v) { not_ready_ = v; }
    void set_console(bool v)   { console_ = v; }
    void set_tx(bool v)        { tx_ = v; }
    void set_rx(bool v)        { rx_ = v; }

    // Debounced: `raw` is the firmware's own instantaneous flag (which can
    // flap in milliseconds); it only becomes visible to resolve() after
    // EPD_GLYPH_INTERFERENCE_DEBOUNCE_MS of continuous truth. Call every
    // tick (or every loop iteration) — cheap, and it is what lets the
    // debounce timer actually elapse.
    void set_interference(bool raw, uint32_t now) {
        if (raw != interference_raw_) {
            interference_raw_ = raw;
            interference_raw_since_ = now;
        }
    }

    bool interference_confirmed(uint32_t now) const {
        return interference_raw_ && (uint32_t)(now - interference_raw_since_) >= EPD_GLYPH_INTERFERENCE_DEBOUNCE_MS;
    }

    // One-shot "sending home" event. Safe to call again while already
    // playing — it extends the window, which is how a rapid, bursty series
    // of beacon sends coalesces into one visible animation instead of
    // restarting/flickering every send.
    void trigger_beacon(uint32_t now) {
        if (beacon_started_ms_ == 0 || (int32_t)(now - beacon_until_) >= 0) {
            beacon_started_ms_ = now; // fresh engagement (wasn't already playing)
        }
        // Signed-difference idiom (matches :550 and resolve()'s :564 below),
        // not a plain magnitude compare -- a plain `candidate > beacon_until_`
        // breaks exactly at the millis() rollover: right after the wrap,
        // candidate is a small absolute value that is numerically LESS than
        // a stale pre-wrap beacon_until_, so the window would silently fail
        // to extend (or the beacon would read as already-lapsed) for calls
        // landing near that boundary.
        uint32_t candidate = now + EPD_GLYPH_BEACON_TOTAL_MS;
        if ((int32_t)(candidate - beacon_until_) > 0) beacon_until_ = candidate;
    }

    EpdGlyphState resolve(uint32_t now) const {
        if (fault_) return EPD_GLYPH_FAULT;
        if (not_ready_) return EPD_GLYPH_NOT_READY;
        if (console_) return EPD_GLYPH_CONSOLE;
        if (interference_confirmed(now)) return EPD_GLYPH_INTERFERENCE;
        if (tx_) return EPD_GLYPH_TRANSMITTING;   // TX wins if TX and RX overlap
        if (rx_) return EPD_GLYPH_RECEIVING;
        if (beacon_started_ms_ != 0 && (int32_t)(now - beacon_until_) < 0) return EPD_GLYPH_BEACON;
        return EPD_GLYPH_STANDBY;
    }

    // Frame index for `state` after `elapsed` ms in that state. Pure
    // function of (state, elapsed) — no hidden mutable state — so the
    // tests can call it directly without a full tick(). Levels that must
    // NOT self-animate (revision note) always return 0 regardless of
    // elapsed; only the bounded bursts (TX/RX/BEACON) depend on elapsed,
    // and all three hold their last frame once the burst completes instead
    // of looping.
    static uint8_t frame_for(EpdGlyphState state, uint32_t elapsed) {
        switch (state) {
            case EPD_GLYPH_TRANSMITTING:
            case EPD_GLYPH_RECEIVING:
                if (elapsed < EPD_GLYPH_BURST_FRAME_MS) return 0;
                if (elapsed < EPD_GLYPH_BURST_FRAME_MS * 2) return 1;
                return 2; // hold C — no further advance, ever
            case EPD_GLYPH_BEACON:
                if (elapsed < EPD_GLYPH_BURST_FRAME_MS) return 0;
                if (elapsed < EPD_GLYPH_BURST_FRAME_MS * 2) return 1;
                return 2; // hold C for the remainder of the window
            case EPD_GLYPH_STANDBY:
            case EPD_GLYPH_FAULT:
            case EPD_GLYPH_INTERFERENCE:
            case EPD_GLYPH_NOT_READY:
            case EPD_GLYPH_CONSOLE:
            default:
                return 0; // static: drawn once on entry, never again
        }
    }

    // Advances the machine. Returns true iff it actually repainted (state
    // changed, or a bounded burst crossed its next frame boundary) —
    // callers should only push pixels to the panel when this is true.
    // EPD_GLYPH_MIN_INTERVAL_MS is a defensive floor against pathological
    // flapping, not the normal cadence (see header note) — under normal
    // operation, real transitions are already seconds-to-minutes apart and
    // this floor never engages.
    bool tick(uint32_t now) {
        if (ticked_once_ && (uint32_t)(now - last_redraw_ms_) < EPD_GLYPH_MIN_INTERVAL_MS) {
            return false;
        }

        EpdGlyphState resolved = resolve(now);
        bool changed = false;

        if (!ticked_once_ || resolved != current_state_) {
            current_state_ = resolved;
            state_entered_ms_ = now;
            frame_index_ = frame_for(resolved, 0);
            changed = true;
        } else {
            uint32_t elapsed = now - state_entered_ms_;
            uint8_t nf = frame_for(current_state_, elapsed);
            if (nf != frame_index_) {
                frame_index_ = nf;
                changed = true;
            }
        }

        if (!changed) return false;

        epd_glyph_render(bitmap_, current_state_, frame_index_, state_entered_ms_);
        last_redraw_ms_ = now;
        ticked_once_ = true;
        partial_count_++;
        return true;
    }

    bool full_refresh_due(uint32_t now) const {
        if (partial_count_ >= EPD_GLYPH_FULL_REFRESH_PARTIAL_COUNT) return true;
        if (last_full_refresh_ms_ == 0) return false;
        return (uint32_t)(now - last_full_refresh_ms_) >= EPD_GLYPH_FULL_REFRESH_MS;
    }

    void mark_full_refresh(uint32_t now) {
        partial_count_ = 0;
        last_full_refresh_ms_ = now;
    }

    EpdGlyphState current_state() const { return current_state_; }
    uint8_t frame_index() const { return frame_index_; }
    const EpdGlyphBitmap &bitmap() const { return bitmap_; }
    uint32_t partial_count() const { return partial_count_; }
    uint32_t last_redraw_ms() const { return last_redraw_ms_; }

private:
    bool fault_, not_ready_, console_, tx_, rx_;
    bool interference_raw_;
    uint32_t interference_raw_since_;
    uint32_t beacon_until_;
    uint32_t beacon_started_ms_;

    EpdGlyphState current_state_;
    uint8_t frame_index_;
    uint32_t state_entered_ms_;

    uint32_t last_redraw_ms_;
    bool ticked_once_;

    uint32_t partial_count_;
    uint32_t last_full_refresh_ms_;

    EpdGlyphBitmap bitmap_;
};

#endif // EPD_GLYPH_H_INCLUDED
