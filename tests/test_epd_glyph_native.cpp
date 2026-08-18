// Native, off-device test harness for EpdGlyph.h — the T-Echo status-glyph
// state machine + renderer. Compiles standalone with g++ (no Arduino/RNS
// deps), same precedent as reticulum-tool/tests/test_firmware_beacon_contract.py
// compiling firmware/rtnode-2400/HealthBeaconPack.h.
//
// Run: g++ -std=c++11 -I.. -o /tmp/test_epd_glyph test_epd_glyph_native.cpp
//      && /tmp/test_epd_glyph
// or:  make test-epd-glyph   (from the firmware root)
//
// Every assertion prints its own PASS/FAIL line and the harness exits
// non-zero on any failure, so this is CI-friendly without pytest.

#include "../EpdGlyph.h"
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

// ---------------------------------------------------------------------------
// 1. Priority table resolves correctly for every combination of
//    simultaneous persistent conditions (64 combos of the 6 booleans).
//    Beacon is tested separately below since it is time-windowed, not a
//    plain level.
// ---------------------------------------------------------------------------
static void test_priority_table() {
    for (int mask = 0; mask < 64; mask++) {
        bool fault        = mask & 1;
        bool not_ready    = mask & 2;
        bool console       = mask & 4;
        bool interference = mask & 8;
        bool tx           = mask & 16;
        bool rx           = mask & 32;

        EpdGlyphMachine m;
        uint32_t now = 100000;
        m.set_fault(fault);
        m.set_not_ready(not_ready);
        m.set_console(console);
        m.set_tx(tx);
        m.set_rx(rx);
        if (interference) {
            // push it well past the debounce window so it's "confirmed"
            m.set_interference(true, now - EPD_GLYPH_INTERFERENCE_DEBOUNCE_MS - 1000);
        } else {
            m.set_interference(false, now);
        }

        EpdGlyphState expected;
        if (fault) expected = EPD_GLYPH_FAULT;
        else if (not_ready) expected = EPD_GLYPH_NOT_READY;
        else if (console) expected = EPD_GLYPH_CONSOLE;
        else if (interference) expected = EPD_GLYPH_INTERFERENCE;
        else if (tx) expected = EPD_GLYPH_TRANSMITTING;
        else if (rx) expected = EPD_GLYPH_RECEIVING;
        else expected = EPD_GLYPH_STANDBY;

        EpdGlyphState got = m.resolve(now);
        if (got != expected) {
            char buf[128];
            snprintf(buf, sizeof(buf), "priority mask=%d expected=%s got=%s", mask,
                     epd_glyph_state_name(expected), epd_glyph_state_name(got));
            CHECK(false, buf);
        }
    }
    printf("test_priority_table: 64 combinations checked\n");
}

// TX beats RX when both true simultaneously.
static void test_tx_beats_rx() {
    EpdGlyphMachine m;
    m.set_tx(true);
    m.set_rx(true);
    CHECK(m.resolve(1000) == EPD_GLYPH_TRANSMITTING, "TX must win over RX when both true");
}

// Beacon sits below RECEIVING/TRANSMITTING but above STANDBY, and expires
// on its own after its window.
static void test_beacon_priority_and_expiry() {
    EpdGlyphMachine m;
    uint32_t t0 = 5000;
    m.trigger_beacon(t0);
    CHECK(m.resolve(t0) == EPD_GLYPH_BEACON, "beacon should show immediately after trigger");
    CHECK(m.resolve(t0 + EPD_GLYPH_BEACON_TOTAL_MS - 1) == EPD_GLYPH_BEACON, "beacon should still show just before its window ends");
    CHECK(m.resolve(t0 + EPD_GLYPH_BEACON_TOTAL_MS + 1) == EPD_GLYPH_STANDBY, "beacon should have lapsed back to STANDBY after its window");

    // Beacon must not preempt TX/RX/interference/console/not_ready/fault.
    EpdGlyphMachine m2;
    m2.trigger_beacon(t0);
    m2.set_tx(true);
    CHECK(m2.resolve(t0) == EPD_GLYPH_TRANSMITTING, "TX must outrank an in-progress beacon");
}

// ---------------------------------------------------------------------------
// 2. Ticker never fires faster than the floor, and does not thrash on
//    rapid condition flapping (coalescing).
// ---------------------------------------------------------------------------
static void test_ticker_floor_and_coalescing() {
    EpdGlyphMachine m;
    uint32_t last_redraw = 0;
    bool first = true;
    int redraws = 0;

    // Simulate 20 seconds of a 50ms-resolution main loop, flapping TX on
    // and off every 100ms (much faster than any real radio and much
    // faster than the floor) to stress-test coalescing.
    for (uint32_t t = 0; t <= 20000; t += 50) {
        m.set_tx((t / 100) % 2 == 0);
        bool redrew = m.tick(t);
        if (redrew) {
            redraws++;
            if (!first) {
                uint32_t gap = t - last_redraw;
                if (gap < EPD_GLYPH_MIN_INTERVAL_MS) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "redraw gap %u ms < floor %u ms", gap, EPD_GLYPH_MIN_INTERVAL_MS);
                    CHECK(false, buf);
                }
            }
            first = false;
            last_redraw = t;
        }
    }
    // Upper bound: over 20000ms, no more than 20000/floor + 2 redraws.
    int max_allowed = 20000 / EPD_GLYPH_MIN_INTERVAL_MS + 2;
    char buf[96];
    snprintf(buf, sizeof(buf), "redraw count %d exceeds bound %d for a %ums flap under a %ums floor",
             redraws, max_allowed, 100u, EPD_GLYPH_MIN_INTERVAL_MS);
    CHECK(redraws <= max_allowed, buf);
    printf("test_ticker_floor_and_coalescing: %d redraws over 20s of 100ms TX flapping (floor=%ums)\n",
           redraws, EPD_GLYPH_MIN_INTERVAL_MS);
}

// ---------------------------------------------------------------------------
// 3. Event states return to the correct previous state.
// ---------------------------------------------------------------------------
static void test_event_returns_to_previous_state() {
    // NOTE: this deliberately uses RECEIVING as the "previous" state, not
    // INTERFERENCE — INTERFERENCE outranks TRANSMITTING in the priority
    // table (FAULT > NOT_READY > CONSOLE > INTERFERENCE > TX > RX > BEACON
    // > STANDBY), so TX can never preempt a confirmed interference
    // condition; an earlier draft of this test asserted the opposite and
    // was simply wrong about the priority order it was itself checking
    // above in test_priority_table(). RX correctly sits below TX, so TX
    // preempting RX and falling back to RX is the faithful version of this
    // test.
    EpdGlyphMachine m;
    uint32_t t = 0;

    // Establish RECEIVING as the "previous" state.
    m.set_rx(true);
    t += EPD_GLYPH_MIN_INTERVAL_MS + 100;
    m.tick(t);
    CHECK(m.current_state() == EPD_GLYPH_RECEIVING, "should have settled on RECEIVING");

    // TX preempts it (TX outranks RX).
    t += EPD_GLYPH_MIN_INTERVAL_MS + 100;
    m.set_tx(true);
    m.tick(t);
    CHECK(m.current_state() == EPD_GLYPH_TRANSMITTING, "TX should preempt RECEIVING");

    // TX clears -> must fall back to RECEIVING (still true), not STANDBY.
    t += EPD_GLYPH_MIN_INTERVAL_MS + 100;
    m.set_tx(false);
    m.tick(t);
    CHECK(m.current_state() == EPD_GLYPH_RECEIVING, "clearing TX should return to RECEIVING, not STANDBY");

    // Now clear RX too -> falls back to STANDBY.
    t += EPD_GLYPH_MIN_INTERVAL_MS + 100;
    m.set_rx(false);
    m.tick(t);
    CHECK(m.current_state() == EPD_GLYPH_STANDBY, "clearing RECEIVING should return to STANDBY");
}

// A TX/RX/BEACON burst is bounded: it holds its last frame and does NOT
// loop back to frame 0 no matter how long the condition remains true.
static void test_bursts_hold_not_loop() {
    uint32_t elapsed_far = 10u * 60u * 1000u; // 10 minutes into an ongoing TX
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_TRANSMITTING, elapsed_far) == 2,
          "TX burst must hold at frame 2 (C) indefinitely, not loop");
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_RECEIVING, elapsed_far) == 2,
          "RX burst must hold at frame 2 (C) indefinitely, not loop");

    // And the always-static states never move off frame 0 regardless of
    // elapsed time (the core of the panel-wear fix).
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_STANDBY, elapsed_far) == 0, "STANDBY must stay static");
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_NOT_READY, elapsed_far) == 0, "NOT_READY must stay static");
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_CONSOLE, elapsed_far) == 0, "CONSOLE must stay static");
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_FAULT, elapsed_far) == 0, "FAULT must stay static");
    CHECK(EpdGlyphMachine::frame_for(EPD_GLYPH_INTERFERENCE, elapsed_far) == 0, "INTERFERENCE must stay static");
}

// A long, unbroken TX condition produces a BOUNDED number of redraws (the
// 3-frame burst), not one every floor-interval for as long as TX stays true
// — this is the actual regression test for the panel-wear fix on the event
// states.
static void test_sustained_tx_is_bounded() {
    EpdGlyphMachine m;
    m.set_tx(true);
    int redraws = 0;
    for (uint32_t t = 0; t <= 5UL * 60UL * 1000UL; t += 250) { // 5 simulated minutes
        if (m.tick(t)) redraws++;
    }
    // Expect: 1 (enter TRANSMITTING, frame A) + up to 2 more (B, C) = at
    // most 3 redraws total for an unbroken 5-minute TX condition.
    char buf[96];
    snprintf(buf, sizeof(buf), "sustained TX produced %d redraws in 5 simulated minutes (expected <= 3)", redraws);
    CHECK(redraws <= 3, buf);
    CHECK(m.current_state() == EPD_GLYPH_TRANSMITTING, "should still be TRANSMITTING");
    CHECK(m.frame_index() == 2, "should be holding at frame C");
    printf("test_sustained_tx_is_bounded: %d redraws for a 5-minute unbroken TX (bound=3)\n", redraws);
}

// Sustained STANDBY produces exactly ONE redraw ever (the panel-wear fix,
// direct check): 24 simulated hours of idle must not touch the panel more
// than once.
static void test_sustained_standby_is_static() {
    EpdGlyphMachine m;
    int redraws = 0;
    for (uint32_t t = 0; t <= 24UL * 60UL * 60UL * 1000UL; t += 60000) { // 24h, 1min steps
        if (m.tick(t)) redraws++;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "24h of STANDBY produced %d redraws (expected exactly 1)", redraws);
    CHECK(redraws == 1, buf);
    printf("test_sustained_standby_is_static: %d redraw(s) over 24 simulated hours idle\n", redraws);
}

// ---------------------------------------------------------------------------
// 4. Interference debounce: flapping under the threshold never confirms;
//    sustained truth past the threshold does.
// ---------------------------------------------------------------------------
static void test_interference_debounce() {
    EpdGlyphMachine m;
    uint32_t t = 0;
    // Flap on/off every 500ms (well under the 3000ms debounce) for 10s.
    for (; t <= 10000; t += 500) {
        m.set_interference((t / 500) % 2 == 0, t);
        CHECK(!m.interference_confirmed(t), "flapping interference under the debounce window must never confirm");
    }
    // Force a clean falling edge, THEN a clean rising edge at a known
    // instant, so interference_raw_since_ is exactly hold_start — set_
    // interference() only records a new "since" timestamp when the value
    // actually changes, so re-asserting the same value the flap loop
    // happened to end on would silently keep the old timestamp.
    m.set_interference(false, t);
    uint32_t hold_start = t + 1;
    m.set_interference(true, hold_start);
    CHECK(!m.interference_confirmed(hold_start + EPD_GLYPH_INTERFERENCE_DEBOUNCE_MS - 100),
          "must not confirm just under the debounce window");
    CHECK(m.interference_confirmed(hold_start + EPD_GLYPH_INTERFERENCE_DEBOUNCE_MS + 100),
          "must confirm once held past the debounce window");
}

// ---------------------------------------------------------------------------
// 5. Speckle density <=15% and dot clear-zone respected; two different
//    seeds (frame 0 vs frame 1 of the legend/half-render path) differ.
// ---------------------------------------------------------------------------
static void test_speckle_density_and_clear_zone() {
    EpdGlyphBitmap b;
    epd_glyph_render(b, EPD_GLYPH_INTERFERENCE, 0, 12345);

    // "Density ~15%" is the SPECKLE FIELD's density (the renderer computes
    // its speckle count as 15% of the pixels outside the dot's clear
    // zone) — the anchor dot sits on top of that and is separate, expected
    // content, not part of the noise budget. Replicate the renderer's own
    // accounting here rather than asserting against the whole 64x64 canvas,
    // so this test tracks the actual contract instead of a magic number.
    int total = EPD_GLYPH_W * EPD_GLYPH_H;
    int outside = 0;
    for (int y = 0; y < EPD_GLYPH_H; y++)
        for (int x = 0; x < EPD_GLYPH_W; x++) {
            int dx = x - EPD_GLYPH_CX, dy = y - EPD_GLYPH_CY;
            if (dx * dx + dy * dy >= EPD_GLYPH_SPECKLE_CLEAR_R * EPD_GLYPH_SPECKLE_CLEAR_R) outside++;
        }
    int expected_speckles = (int)(EPD_GLYPH_SPECKLE_DENSITY * outside);

    EpdGlyphBitmap dot_only;
    dot_only.clear();
    epd_glyph_draw::fill_circle(dot_only, EPD_GLYPH_CX, EPD_GLYPH_CY, EPD_GLYPH_DOT_R);
    int dot_pixels = dot_only.count_set();

    int set_count = b.count_set();
    int speckle_pixels = set_count - dot_pixels;
    char buf[160];
    snprintf(buf, sizeof(buf), "speckle pixel count %d does not match renderer's own density*outside formula (%d)",
             speckle_pixels, expected_speckles);
    CHECK(speckle_pixels == expected_speckles, buf);

    char buf2[128];
    snprintf(buf2, sizeof(buf2), "speckle density %.4f exceeds the 15%% cap (%d of %d canvas pixels)",
             (double)speckle_pixels / total, speckle_pixels, total);
    CHECK(speckle_pixels <= (int)(EPD_GLYPH_SPECKLE_DENSITY * total) + 1, buf2);

    // Clear zone: no set pixel within EPD_GLYPH_SPECKLE_CLEAR_R of centre
    // EXCEPT the dot itself (radius EPD_GLYPH_DOT_R < clear radius).
    bool violation = false;
    for (int y = 0; y < EPD_GLYPH_H; y++) {
        for (int x = 0; x < EPD_GLYPH_W; x++) {
            if (!b.get(x, y)) continue;
            int dx = x - EPD_GLYPH_CX, dy = y - EPD_GLYPH_CY;
            int dist2 = dx * dx + dy * dy;
            if (dist2 >= EPD_GLYPH_SPECKLE_CLEAR_R * EPD_GLYPH_SPECKLE_CLEAR_R) continue; // outside clear zone, fine
            if (dist2 > EPD_GLYPH_DOT_R * EPD_GLYPH_DOT_R) violation = true; // inside clear zone but outside the dot itself
        }
    }
    CHECK(!violation, "a speckle pixel was placed inside the dot's clear zone");

    // Different frames must use different seeds -> different patterns.
    EpdGlyphBitmap b2;
    epd_glyph_render(b2, EPD_GLYPH_INTERFERENCE, 1, 12345);
    CHECK(!b.equals(b2), "interference frame 0 and frame 1 should render different speckle fields");
}

// ---------------------------------------------------------------------------
// 6. Periodic full refresh triggers on schedule (count OR time, whichever
//    first).
// ---------------------------------------------------------------------------
static void test_full_refresh_schedule_by_count() {
    EpdGlyphMachine m;
    CHECK(!m.full_refresh_due(0), "should not be due with zero partials");
    // Force EPD_GLYPH_FULL_REFRESH_PARTIAL_COUNT distinct redraws via
    // alternating tx on/off, each separated past the floor.
    uint32_t t = 0;
    bool on = false;
    for (uint32_t i = 0; i < EPD_GLYPH_FULL_REFRESH_PARTIAL_COUNT; i++) {
        t += EPD_GLYPH_MIN_INTERVAL_MS + 10;
        on = !on;
        m.set_tx(on);
        bool redrew = m.tick(t);
        CHECK(redrew, "expected a redraw on each alternation past the floor");
    }
    CHECK(m.full_refresh_due(t), "full refresh should be due once partial_count reaches the threshold");
    m.mark_full_refresh(t);
    CHECK(!m.full_refresh_due(t), "full refresh should not be due immediately after being marked done");
}

static void test_full_refresh_schedule_by_time() {
    EpdGlyphMachine m;
    m.set_fault(true);
    m.tick(1000); // one redraw, partial_count=1, well under the count threshold
    m.mark_full_refresh(1000);
    CHECK(!m.full_refresh_due(1000 + EPD_GLYPH_FULL_REFRESH_MS - 1), "must not be due just before the time threshold");
    CHECK(m.full_refresh_due(1000 + EPD_GLYPH_FULL_REFRESH_MS + 1), "must be due once the time threshold elapses, even with few partials");
}

// ---------------------------------------------------------------------------
// 7. Pixel-pattern sanity for each state: the dot is present in every
//    state (including FAULT, "struck through" not "gone"), and each
//    state's frame sequence is visibly distinct from a blank canvas.
// ---------------------------------------------------------------------------
static void test_dot_present_in_every_state() {
    EpdGlyphState states[] = {
        EPD_GLYPH_STANDBY, EPD_GLYPH_TRANSMITTING, EPD_GLYPH_RECEIVING,
        EPD_GLYPH_BEACON, EPD_GLYPH_FAULT, EPD_GLYPH_INTERFERENCE,
        EPD_GLYPH_NOT_READY, EPD_GLYPH_CONSOLE
    };
    for (EpdGlyphState s : states) {
        EpdGlyphBitmap b;
        epd_glyph_render(b, s, 0, 555);
        // The dot occupies a disc of radius EPD_GLYPH_DOT_R around either
        // the main centre (most states) or the left-third point (BEACON).
        int cx = (s == EPD_GLYPH_BEACON) ? 16 : EPD_GLYPH_CX;
        int cy = EPD_GLYPH_CY;
        bool any_set_near_centre = false;
        for (int y = cy - EPD_GLYPH_DOT_R; y <= cy + EPD_GLYPH_DOT_R; y++)
            for (int x = cx - EPD_GLYPH_DOT_R; x <= cx + EPD_GLYPH_DOT_R; x++)
                if (b.get(x, y)) any_set_near_centre = true;
        char buf[96];
        snprintf(buf, sizeof(buf), "%s: anchor dot must be present", epd_glyph_state_name(s));
        CHECK(any_set_near_centre, buf);
    }
}

static void test_tx_and_rx_are_visually_opposite() {
    // Frame C (fully out): TX should have arc pixels near the edge (r=30)
    // that RX's frame A (r=30 only, same ring) also has — but TX should be
    // predominantly built from the inside out and RX from the outside in.
    // Concretely: TX frame 0 has ONLY the r=14 ring; RX frame 0 has ONLY
    // the r=30 ring. These must differ.
    EpdGlyphBitmap tx0, rx0;
    epd_glyph_render(tx0, EPD_GLYPH_TRANSMITTING, 0, 0);
    epd_glyph_render(rx0, EPD_GLYPH_RECEIVING, 0, 0);
    CHECK(!tx0.equals(rx0), "TX frame A (r=14) and RX frame A (r=30) must not render identically");

    // TX frame C and RX frame C both draw all three rings (14,22,30) so the
    // FINAL held frame is the same fully-formed burst shape either way —
    // that's expected/fine; the opposition is in the build-up direction
    // (inside-out vs outside-in), which frame-by-frame progression encodes.
    EpdGlyphBitmap tx1, rx1;
    epd_glyph_render(tx1, EPD_GLYPH_TRANSMITTING, 1, 0);
    epd_glyph_render(rx1, EPD_GLYPH_RECEIVING, 1, 0);
    CHECK(!tx1.equals(rx1), "TX frame B (r=14,22) and RX frame B (r=30,22) must not render identically");
}

int main() {
    test_priority_table();
    test_tx_beats_rx();
    test_beacon_priority_and_expiry();
    test_ticker_floor_and_coalescing();
    test_event_returns_to_previous_state();
    test_bursts_hold_not_loop();
    test_sustained_tx_is_bounded();
    test_sustained_standby_is_static();
    test_interference_debounce();
    test_speckle_density_and_clear_zone();
    test_full_refresh_schedule_by_count();
    test_full_refresh_schedule_by_time();
    test_dot_present_in_every_state();
    test_tx_and_rx_are_visually_opposite();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
