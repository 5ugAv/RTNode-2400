// TbField.h — Jonesey's directional status field, extracted from the proven
// ~/overlay_test tracker build (operator, 2026-08-21: the T114's screen is
// A REPLICA OF JONESEY'S GRAPHICS). The engine draws into a GFXcanvas16 and
// is driver-independent; only the final blit differs per board. All physics,
// timings and the noise-floor calibration bar are UNCHANGED from the build
// running on Jonesey today. Geometry is per-board below.

// ---- Heltec Wireless Tracker: directional status field (v4) ------------------
// TX=out(overshoot), RX=in(dense), interference=dense disorder, probe=ping train,
// error=explosion-in then a persistent flickering FAULT block, idle=subtle breath
// only while radio_online. Colour code untouched (reads npr/npg/npb).
#if BOARD_MODEL == BOARD_HELTEC_WIRELESS_TRACKER || BOARD_MODEL == BOARD_HELTEC_T114
#include <math.h>
#include <Adafruit_GFX.h>   // GFXcanvas16 — the engine's offscreen canvas
extern uint8_t npr, npg, npb;
extern int last_rssi;
extern int current_rssi, noise_floor;
extern bool noise_floor_sampled;   // false until 128 samples collected / after an RF recal
extern bool radio_online;   // true once the host sends CMD_RADIO_STATE=on (Reticulum opened us)

// ---- tunables ---------------------------------------------------------------
#if BOARD_MODEL == BOARD_HELTEC_T114
// T114 port (2026-08-21): Heltec's own doc says the panel is 1.14",
// 135(H)x240(V) — NOT the 240x240 a stale comment claimed. Portrait field,
// same physics; TB_CY/tb_maxr derive from these automatically.
#define TB_W             135
#define TB_H             240
#define TB_BAR_H         6                    // calibration bar height (bottom edge)
#define TB_FIELD_H       (TB_H - TB_BAR_H)
#define TB_CX            67
#else
#define TB_W             160
#define TB_H             80
#define TB_BAR_H         5                    // calibration bar height (bottom edge)
#define TB_FIELD_H       (TB_H - TB_BAR_H)    // 75: animation field, above the bar
#define TB_CX            80
#endif
#define TB_CY            (TB_FIELD_H / 2)      // 37: recentred to make room for the bar
#define TB_PARTS         180
#define TB_BLK           2
#define TB_CORE_R        4
// TB_MAX_R is derived from the field geometry at runtime (tb_maxr, set in tb_setup)
#if BOARD_MODEL == BOARD_HELTEC_T114
#define TB_OUT_MAX       260.0f  // TX overshoots past the ~135px corner radius
#else
#define TB_OUT_MAX       135.0f  // TX overshoots well past the 89px screen corner
#endif
#define TB_KICK          26.0f
#define TB_SPRING        0.055f
#define TB_DAMP          0.87f
#define TB_SPD_POW       1.4f
#define TB_FRAME_MS      40
#define TB_RECOLOUR_FRAC 0.5f
#define TB_IN_FRAC       0.82f   // RX recruits more -> denser blue
#define TB_NOISE_FRAC    0.95f   // interference recruits nearly all -> dense
#define TB_FAULT_FRAC    0.88f
#define TB_COHORT_MS     850     // longer so the TX second pulse fully blooms
#define TB_IN_MS         1700    // RX lingers ~2x (match the V4 LED blue flash length)
#define TB_FAULT_MS      1500    // fault particle-cohort life (the explosion-in)
#define TB_FAULT_HOLD    1500    // base ms the fault block holds after forming
#define TB_FAULT_TAIL    300     // per-frame extension while the LED stays red
#define TB_MAX_COHORTS   3
#define TB_EDGE_FADE     60
#define TB_CORE_GAIN     1.4f
#define TB_AMP_TX        1.05f   // stronger TX drive so amber clears the edge
#define TB_TRAIL         5
#define TB_TRAIL_STEP    3.2f
#define TB_PROBE_K       0.80f
#define TB_PULSES        2       // TX/RX give two pulses per event
#define TB_PULSE_GAP     240     // ms between the two pulses
// interference -> disorder
#define TB_INTERF_MIN    11
#define TB_INTERF_MAX    45
#define TB_JITTER_GAIN   1.30f
#define TB_NOISE_PXL     12.0f
// error -> fault block
#if BOARD_MODEL == BOARD_HELTEC_T114
#define TB_FAULT_W       110
#define TB_FAULT_H       96
#else
#define TB_FAULT_W       132
#define TB_FAULT_H       60
#endif
// idle "online" breathing (only when radio_online = Reticulum has opened this RNode)
#define TB_BREATH_MS     4200    // breath period (slow, calm)
#define TB_BREATH_MIN    6       // centre brightness at exhale (tight/dim)
#define TB_BREATH_MAX    48      // centre brightness at inhale (wide/bright)
#define TB_BREATH_S0     3.0f    // gaussian glow sigma at exhale (px)
#define TB_BREATH_S1     8.0f    // gaussian glow sigma at inhale (px)
#define TB_BREATH_R      40      // colour: soft aqua = "on the mesh"
#define TB_BREATH_G      150
#define TB_BREATH_B      120
// noise floor -> ambient haze (background) + calibration bar. Anchored to the
// antenna-#4 suburban benchmark (-102 dBm ~= "almost clear"); quieter reads clear.
#define TB_NOISE_MIN     -108    // dBm: fully clear at/below (quiet / rural)
#define TB_NOISE_MAX     -85     // dBm: full heavy static (bad site)
#define TB_HAZE_GAMMA    2.2f    // >1 keeps the benchmark almost-clear, ramps toward dirty
#define TB_HAZE_DENSITY  0.10f   // max fraction of field pixels hazed at full scale
#define TB_HAZE_BRIGHT   70      // max haze pixel brightness (0-255)
#define TB_HAZE_MIN_BRIGHT 28    // floor so a drawn speck is faintly visible, not rounded to black
#define TB_HAZE_MS       150     // re-seed the haze pattern this often (held between = no strobe)
#define TB_HAZE_CLEAR_R  14      // px kept clear around the heartbeat dot
#define TB_STRIP_MS      300     // calibration bar update interval
#define TB_DEMO          0
#define TB_DEMO_BREATH_START 13600  // demo: show the breathing after the fault clears
// -- motion modes --
#define TB_OUT   0
#define TB_IN    1
#define TB_PROBE 2
#define TB_NOISE 3               // interference (disorder-scaled, dense)
#define TB_FAULT 4               // error (explosion in -> persistent red block)
// -----------------------------------------------------------------------------

// LAZY canvas (T114 port rev 2): a 63KB static-init heap allocation is
// exactly the pre-main allocation the T-Echo saga proved treacherous on
// nRF52. Constructed on the first burst call instead, when the heap is
// provably up. On allocation failure the panel is painted DARK RED — an
// honest visible failure signal, never a silent black screen.
static GFXcanvas16 *tb_canvas_p = NULL;
#define tb_canvas (*tb_canvas_p)
struct TbCohort { uint16_t col; uint32_t born; bool active; uint8_t mode; float disorder;
                  uint32_t repulse_at; uint8_t repulses_left; float pamp; };
struct TbPart   { float ang, spd01, r, vr; int8_t coh; };
static TbCohort tb_coh[TB_MAX_COHORTS];
static TbPart   tb_part[TB_PARTS];
static bool     tb_init = false;
static uint32_t tb_last_frame = 0;
static uint8_t  tb_lr = 0, tb_lg = 0, tb_lb = 0;
static float    tb_maxr = 88.0f;    // = hypot(TB_CX, TB_CY) for the field; set in tb_setup
#define TB_MAX_R tb_maxr
static float    tb_bar_norm = 0.0f; // held calibration-bar level
static uint32_t tb_bar_last = 0;
static uint32_t tb_fault_born = 0, tb_fault_until = 0;

static inline uint16_t tb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)((r & 0xF8) << 8) | (uint16_t)((g & 0xFC) << 3) | (uint16_t)(b >> 3);
}
static inline uint16_t tb_scale(uint16_t c, int bright) {
  if (bright < 0) bright = 0; if (bright > 255) bright = 255;
  int r = ((c >> 11) & 0x1F) * bright / 255;
  int g = ((c >>  5) & 0x3F) * bright / 255;
  int b = ( c        & 0x1F) * bright / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
static inline uint32_t tb_life(uint8_t mode) {
  if (mode == TB_FAULT) return TB_FAULT_MS;
  if (mode == TB_IN)    return TB_IN_MS;
  return TB_COHORT_MS;
}
static inline bool tb_is_red(uint8_t r, uint8_t g, uint8_t b) {
  return (r > 60 && g < 40 && b < 40);
}
static uint8_t tb_mode_for(uint8_t r, uint8_t g, uint8_t b) {
  if (tb_is_red(r, g, b))                      return TB_FAULT;   // red    -> error
  if (g > 180 && r < 120 && b < 160)           return TB_PROBE;   // v.green-> health probe
  if (b > r && b > g)                          return TB_IN;      // blue   -> RX
  if (r > 60 && b > 60 && g < r/2 && g < b)    return TB_NOISE;   // purple -> interference
  return TB_OUT;                                                  // amber  -> TX
}
static void tb_setup() {
  tb_maxr = sqrtf((float)(TB_CX*TB_CX + TB_CY*TB_CY));   // re-derived from the field
  for (int i = 0; i < TB_PARTS; i++) {
    tb_part[i].ang   = random(0, 62832) / 10000.0f;
    float u = random(0, 1000) / 1000.0f;
    tb_part[i].spd01 = powf(u, TB_SPD_POW);
    tb_part[i].r = 0; tb_part[i].vr = 0; tb_part[i].coh = -1;
  }
  for (int c = 0; c < TB_MAX_COHORTS; c++) tb_coh[c].active = false;
}
static int tb_active_particles() {
  int n = 0;
  for (int i = 0; i < TB_PARTS; i++)
    if (tb_part[i].coh >= 0 && tb_coh[tb_part[i].coh].active) n++;
  return n;
}
static void tb_fire(uint32_t now, uint16_t col, float amp, uint8_t mode, float disorder) {
  int reclaimed = -1, slot = -1;
  for (int c = 0; c < TB_MAX_COHORTS; c++) if (!tb_coh[c].active) { slot = c; break; }
  if (slot < 0) { slot = 0;
    for (int c = 1; c < TB_MAX_COHORTS; c++) if (tb_coh[c].born < tb_coh[slot].born) slot = c;
    reclaimed = slot; }
  tb_coh[slot].col=col; tb_coh[slot].born=now; tb_coh[slot].active=true;
  tb_coh[slot].mode=mode; tb_coh[slot].disorder=disorder;
  if (mode == TB_FAULT) { tb_fault_born = now; tb_fault_until = now + TB_FAULT_HOLD; }
  tb_coh[slot].pamp = amp;                                        // TX double-pulses; RX is driven
  if (mode == TB_OUT) {                                           // by the radio signal, not forced
    tb_coh[slot].repulses_left = TB_PULSES - 1;
    tb_coh[slot].repulse_at = now + TB_PULSE_GAP;
  } else {
    tb_coh[slot].repulses_left = 0;
  }
  float frac = TB_RECOLOUR_FRAC;
  if (mode == TB_IN) frac = TB_IN_FRAC;
  else if (mode == TB_NOISE) frac = TB_NOISE_FRAC;
  else if (mode == TB_FAULT) frac = TB_FAULT_FRAC;
  bool first = (tb_active_particles() == 0);
  for (int i = 0; i < TB_PARTS; i++) {
    bool take = first
             || (reclaimed >= 0 && tb_part[i].coh == reclaimed)
             || (random(0, 1000) / 1000.0f < frac);
    if (!take) continue;
    TbPart &p = tb_part[i]; p.coh = slot;
    if (mode == TB_OUT) {
      p.r = 0; p.vr = amp * p.spd01 * TB_KICK;                     // wave from centre, outward only
    } else if (mode == TB_IN) {
      p.r = (0.55f + 0.45f * p.spd01) * TB_MAX_R; p.vr = -amp * p.spd01 * TB_KICK;
    } else if (mode == TB_PROBE) {                                 // uniform symmetrical ring
      p.r = 0; p.vr += amp * (0.62f + 0.30f * p.spd01) * TB_KICK * TB_PROBE_K;
    } else if (mode == TB_FAULT) {                                 // explosion in from outside
      p.r = (0.65f + 0.35f * p.spd01) * TB_MAX_R; p.vr = -amp * p.spd01 * TB_KICK;
    } else {                                                       // TB_NOISE: wide spread, out
      p.r = (0.15f + 0.75f * p.spd01) * TB_MAX_R;
      p.vr = amp * p.spd01 * TB_KICK * 0.5f;
    }
  }
}
static void tb_event(uint32_t now, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t mode = tb_mode_for(r, g, b);
  float amp = TB_AMP_TX, disorder = 0.0f;
  if (mode == TB_IN) {
    int s = last_rssi; if (s < -115) s = -115; if (s > -45) s = -45;
    amp = 0.15f + (s + 115) * (1.0f - 0.15f) / 70.0f;
  } else if (mode == TB_FAULT) {
    disorder = 1.0f; amp = 0.75f;
  } else if (mode == TB_NOISE) {
    int lvl = current_rssi - noise_floor;                        // fresh: 3ms sampling
    if (lvl < TB_INTERF_MIN) lvl = TB_INTERF_MIN;
    if (lvl > TB_INTERF_MAX) lvl = TB_INTERF_MAX;
    disorder = (float)(lvl - TB_INTERF_MIN) / (TB_INTERF_MAX - TB_INTERF_MIN);
    amp = 0.35f + 0.30f * disorder;
  }
  tb_fire(now, tb565(r, g, b), amp, mode, disorder);
}

#if TB_DEMO
struct TbDemoStep { uint32_t t; uint8_t r, g, b; float amp, dis; };
static const TbDemoStep tb_demo_script[] = {
  {0,     0,   0,   255, 0.90f, 0.0f},   // blue RX (converge in, dense)
  {1600,  255, 80,  0  , 1.00f, 0.0f},   // orange TX (bloom out past edge)
  {3200,  255, 80,  0  , 1.00f, 0.0f},   // orange TX ...
  {3330,  0,   0,   255, 0.90f, 0.0f},   //   + blue RX -> round trip
  {5000,  144, 0,   112, 0.45f, 0.25f},  // purple interference: MILD (near-clean)
  {6600,  144, 0,   112, 0.65f, 0.95f},  // purple interference: SEVERE (dense chaos)
  {8200,  40,  255, 120, 0.90f, 0.0f},   // green probe pulse 1
  {8430,  40,  255, 120, 0.90f, 0.0f},   // green probe pulse 2
  {8660,  40,  255, 120, 0.90f, 0.0f},   // green probe pulse 3
  {8890,  40,  255, 120, 0.90f, 0.0f},   // green probe pulse 4
  {9120,  40,  255, 120, 0.90f, 0.0f},   // green probe pulse 5
  {9900,  255, 0,   0  , 0.75f, 1.0f},   // red error: explosion in -> PERSISTENT block
};
#define TB_DEMO_LEN 18800
#define TB_DEMO_FAULT_HOLD 3400          // demo: hold the fault block to show persistence
static int tb_demo_idx = 0;
static uint32_t tb_demo_start = 0, tb_demo_lastt = 0, tb_demo_t = 0;
static void tb_demo_step(uint32_t now) {
  if (tb_demo_start == 0) tb_demo_start = now;
  uint32_t t = (now - tb_demo_start) % TB_DEMO_LEN;
  tb_demo_t = t;
  if (t < tb_demo_lastt) tb_demo_idx = 0;
  tb_demo_lastt = t;
  int n = sizeof(tb_demo_script) / sizeof(tb_demo_script[0]);
  while (tb_demo_idx < n && tb_demo_script[tb_demo_idx].t <= t) {
    TbDemoStep s = tb_demo_script[tb_demo_idx++];
    uint8_t mode = tb_mode_for(s.r, s.g, s.b);
    tb_fire(now, tb565(s.r, s.g, s.b), s.amp, mode, s.dis);
    if (mode == TB_FAULT) tb_fault_until = now + TB_DEMO_FAULT_HOLD;   // persist for the demo
  }
}
#endif

void tracker_status_burst() {
  uint32_t now = millis();
  if (now - tb_last_frame < TB_FRAME_MS) return;
  tb_last_frame = now;
  if (!tb_init) {
    if (tb_canvas_p == NULL) tb_canvas_p = new GFXcanvas16(TB_W, TB_H);
    if (tb_canvas_p == NULL || tb_canvas_p->getBuffer() == NULL) {
      // allocation failed: paint the panel dark red once a second — a
      // visible, honest fault signal (a black screen tells nobody anything)
      static uint32_t last_fail_paint = 0;
      if (now - last_fail_paint > 1000) {
        last_fail_paint = now;
#if BOARD_MODEL == BOARD_HELTEC_T114
        static uint16_t failband[TB_W * 8];
        for (int i = 0; i < TB_W * 8; i++) failband[i] = 0x8800;  // dark red
        display.blit565(failband, TB_W, 8);
#endif
      }
      return;
    }
    tb_setup(); tb_init = true;
  }

  uint8_t r = npr, g = npg, b = npb;
#if TB_DEMO
  tb_demo_step(now);
#else
  // TX FIX (2026-07-31): led_tx_on/off both happen inside ONE loop pass, so
  // the colour sampler below can never see the amber - real transmissions were
  // invisible on the TFT. Consume the firmware's own display_tx latch instead.
  if (display_tx) { display_tx = false; tb_event(now, 255, 80, 0); }
  int dr=(int)r-tb_lr, dg=(int)g-tb_lg, db=(int)b-tb_lb;
  if (((dr*dr+dg*dg+db*db) > (28*28)) && (r>24||g>24||b>24)) tb_event(now, r, g, b);
  tb_lr=r; tb_lg=g; tb_lb=b;
  if (tb_is_red(r, g, b)) {                        // LED steady red -> fault ongoing, keep block
    if (tb_fault_born == 0 || now > tb_fault_until) tb_fault_born = now;
    tb_fault_until = now + TB_FAULT_TAIL;
  }
#endif

  int active_coh = 0;
  for (int c = 0; c < TB_MAX_COHORTS; c++) {
    if (tb_coh[c].active && (now - tb_coh[c].born) < tb_life(tb_coh[c].mode)) active_coh++;
    else tb_coh[c].active = false;
  }
  for (int i = 0; i < TB_PARTS; i++)
    if (tb_part[i].coh >= 0 && !tb_coh[tb_part[i].coh].active) tb_part[i].coh = -1;
  bool fault_active = (tb_fault_born != 0) && (now < tb_fault_until);

  // TX second pulse: re-kick the cohort's particles outward after a short gap
  for (int c = 0; c < TB_MAX_COHORTS; c++) {
    if (!tb_coh[c].active || tb_coh[c].repulses_left == 0 || now < tb_coh[c].repulse_at) continue;
    float a = tb_coh[c].pamp;
    for (int i = 0; i < TB_PARTS; i++) {
      if (tb_part[i].coh != c) continue;
      tb_part[i].r = 0; tb_part[i].vr = a * tb_part[i].spd01 * TB_KICK;  // second wave from centre
    }
    tb_coh[c].repulses_left--;
    tb_coh[c].repulse_at = now + TB_PULSE_GAP;
  }

  tb_canvas.fillScreen(0x0000);

  // --- noise floor -> ambient haze, drawn FIRST so everything composites over it ---
  float nf_norm = 0.0f;
  if (noise_floor_sampled) {
    float t = (float)(noise_floor - TB_NOISE_MIN) / (float)(TB_NOISE_MAX - TB_NOISE_MIN);
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    nf_norm = t;
  }
  float haze01 = powf(nf_norm, TB_HAZE_GAMMA);
  if (haze01 > 0.001f) {
    int nhz = (int)(TB_HAZE_DENSITY * haze01 * TB_W * TB_FIELD_H);
    uint16_t hazecol = tb565(200, 210, 225);
    uint32_t hz = (millis() / TB_HAZE_MS) * 2654435761u;    // held ~150ms, then re-seeds
    int clr2 = TB_HAZE_CLEAR_R * TB_HAZE_CLEAR_R;
    for (int k = 0; k < nhz; k++) {
      hz ^= hz << 13; hz ^= hz >> 17; hz ^= hz << 5; int x = (int)(hz % TB_W);
      hz ^= hz << 13; hz ^= hz >> 17; hz ^= hz << 5; int y = (int)(hz % TB_FIELD_H);
      hz ^= hz << 13; hz ^= hz >> 17; hz ^= hz << 5;
      float jit = 0.45f + 0.55f * (((hz >> 8) & 0xFF) / 255.0f);
      int br = TB_HAZE_MIN_BRIGHT + (int)((TB_HAZE_BRIGHT - TB_HAZE_MIN_BRIGHT) * haze01 * jit);
      int dx = x - TB_CX, dy = y - TB_CY;
      if (dx*dx + dy*dy < clr2) continue;                   // keep clear around the heartbeat
      tb_canvas.drawPixel(x, y, tb_scale(hazecol, br));
    }
  }

  int cc[TB_MAX_COHORTS] = {0};
  for (int i = 0; i < TB_PARTS; i++) {
    TbPart &p = tb_part[i];
    if (p.coh < 0 || !tb_coh[p.coh].active) {                      // unowned: settle silently
      p.vr += -TB_SPRING * p.r; p.vr *= TB_DAMP; p.r += p.vr;
      if (p.r < 0) { p.r = 0; p.vr = 0; }
      continue;
    }
    cc[p.coh]++;
    uint8_t mode = tb_coh[p.coh].mode;
    uint16_t col = tb_coh[p.coh].col;
    if (mode == TB_OUT) {                                         // TX: outward only, no bounce-back
      p.r += p.vr;                                                // ballistic; flies off the edge
      if (p.r > TB_OUT_MAX) p.r = TB_OUT_MAX;                     // park off-screen; cohort end resets
    } else {
      p.vr += -TB_SPRING * p.r; p.vr *= TB_DAMP; p.r += p.vr;     // spring modes (RX/probe)
      if (p.r < 0) { p.r = 0; p.vr = 0; }
      if (p.r > TB_MAX_R) { p.r = TB_MAX_R; p.vr = 0; }
    }

    if (mode == TB_NOISE || mode == TB_FAULT) {                    // disorder-scaled scatter
      float dis = tb_coh[p.coh].disorder;
      float ja = (random(-1000, 1001) / 1000.0f) * dis * TB_JITTER_GAIN;
      int nx = (int)((random(-1000, 1001) / 1000.0f) * dis * TB_NOISE_PXL);
      int ny = (int)((random(-1000, 1001) / 1000.0f) * dis * TB_NOISE_PXL);
      int x = TB_CX + (int)(cosf(p.ang + ja) * p.r) + nx;
      int y = TB_CY + (int)(sinf(p.ang + ja) * p.r) + ny;
      if (x < 0 || y < 0 || x > TB_W - TB_BLK || y > TB_H - TB_BLK) continue;
      int bright = 255 - (int)(p.r / TB_MAX_R * TB_EDGE_FADE);
      uint16_t pc = tb_scale(col, bright);
      tb_canvas.fillRect(x, y, TB_BLK, TB_BLK, pc);
      if (mode == TB_NOISE) {                                      // purple: second block = 2x pixels
        int x2 = x + ((random(0, 2)) ? TB_BLK : -TB_BLK);
        int y2 = y + ((random(0, 2)) ? TB_BLK : -TB_BLK);
        if (x2 >= 0 && y2 >= 0 && x2 <= TB_W - TB_BLK && y2 <= TB_H - TB_BLK)
          tb_canvas.fillRect(x2, y2, TB_BLK, TB_BLK, pc);
      }
      continue;
    }
    // OUT / IN / PROBE: radial + comet tail
    int lead = 255 - (int)(p.r / TB_MAX_R * TB_EDGE_FADE);
    if (mode == TB_IN) {                                           // RX lingers + fades over 2x life
      float agef = 1.0f - (float)(now - tb_coh[p.coh].born) / TB_IN_MS;
      if (agef < 0.0f) agef = 0.0f;
      lead = (int)(lead * agef);
    }
    float dir = (p.vr >= 0.0f) ? 1.0f : -1.0f;
    for (int t = TB_TRAIL - 1; t >= 0; t--) {
      float rr = p.r - dir * t * TB_TRAIL_STEP; if (rr < 0) rr = 0;
      int x = TB_CX + (int)(cosf(p.ang) * rr);
      int y = TB_CY + (int)(sinf(p.ang) * rr);
      if (x < 0 || y < 0 || x > TB_W - TB_BLK || y > TB_H - TB_BLK) continue;
      tb_canvas.fillRect(x, y, TB_BLK, TB_BLK, tb_scale(col, lead * (TB_TRAIL - t) / TB_TRAIL));
    }
  }

  // additive core (node) -- only while a non-fault burst is running; never idle
  if (active_coh > 0 && !fault_active) {
    float cr=0, cg=0, cb=0;
    for (int c = 0; c < TB_MAX_COHORTS; c++) {
      if (!tb_coh[c].active) continue;
      float w = 1.0f - (float)(now - tb_coh[c].born) / tb_life(tb_coh[c].mode); if (w < 0) w = 0;
      float sh = (float)cc[c] / TB_PARTS; uint16_t col = tb_coh[c].col;
      cr += ((col>>11)&0x1F)*(255.0f/31)*sh*w;
      cg += ((col>> 5)&0x3F)*(255.0f/63)*sh*w;
      cb += ( col     &0x1F)*(255.0f/31)*sh*w;
    }
    cr*=TB_CORE_GAIN; cg*=TB_CORE_GAIN; cb*=TB_CORE_GAIN;
    uint16_t core_col = tb565(cr>255?255:cr, cg>255?255:cg, cb>255?255:cb);
    tb_canvas.fillCircle(TB_CX, TB_CY, TB_CORE_R, core_col);
  }

  // idle breathing -- subtle "alive" pulse ONLY while Reticulum has this RNode online
  if (active_coh == 0 && !fault_active) {
#if TB_DEMO
    bool tb_online = (tb_demo_t >= TB_DEMO_BREATH_START);
#else
    bool tb_online = radio_online;
#endif
    if (tb_online) {
      float ph  = (now % TB_BREATH_MS) / (float)TB_BREATH_MS;
      float b01 = 0.5f - 0.5f * cosf(2.0f * 3.14159265f * ph);    // smooth 0..1
      float sigma = TB_BREATH_S0 + b01 * (TB_BREATH_S1 - TB_BREATH_S0);  // width breathes
      int   peak  = TB_BREATH_MIN + (int)(b01 * (TB_BREATH_MAX - TB_BREATH_MIN));
      uint16_t bcol = tb565(TB_BREATH_R, TB_BREATH_G, TB_BREATH_B);
      float inv2s2 = 1.0f / (2.0f * sigma * sigma);
      int rad = (int)(2.6f * sigma) + 1;                          // gaussian glow: no hard edge,
      for (int dy = -rad; dy <= rad; dy++) {                      // grows/shrinks into its own fade
        int y = TB_CY + dy; if (y < 0 || y >= TB_H) continue;
        for (int dx = -rad; dx <= rad; dx++) {
          int x = TB_CX + dx; if (x < 0 || x >= TB_W) continue;
          int br = (int)(peak * expf(-(float)(dx*dx + dy*dy) * inv2s2));
          if (br > 0) tb_canvas.drawPixel(x, y, tb_scale(bcol, br));
        }
      }
    }
  }

  // FAULT block -- forms after the explosion, holds + flickers while the fault persists
  if (fault_active) {
    uint32_t age = now - tb_fault_born;
    float ramp = ((float)age - 0.30f * TB_FAULT_MS) / (0.30f * TB_FAULT_MS); // grows 30%..60%
    if (ramp < 0) ramp = 0; if (ramp > 1) ramp = 1;
    if (ramp > 0.0f) {
      int w = (int)(ramp * TB_FAULT_W), h = (int)(ramp * TB_FAULT_H);
      int bright = (ramp >= 1.0f) ? 255 - random(0, 90) : 255;    // flicker once full
      tb_canvas.fillRect(TB_CX - w/2, TB_CY - h/2, w, h, tb_scale(tb565(255,0,0), bright));
      tb_canvas.drawRect(TB_CX - w/2, TB_CY - h/2, w, h, tb565(60,0,0));
    }
  } else {
    tb_fault_born = 0;
  }

  // --- calibration bar (bottom edge): honest LINEAR noise level, held steady ---
  if (now - tb_bar_last >= TB_STRIP_MS) { tb_bar_norm = nf_norm; tb_bar_last = now; }
  {
    int by = TB_H - TB_BAR_H;
    tb_canvas.fillRect(0, by, TB_W, TB_BAR_H, tb565(18, 18, 22));      // track
    if (noise_floor_sampled) {
      int fw = (int)(tb_bar_norm * TB_W); if (fw < 1 && tb_bar_norm > 0.0f) fw = 1;
      uint8_t r_, g_, b_;                                              // green -> amber -> red
      if (tb_bar_norm < 0.5f) { float u = tb_bar_norm / 0.5f;
        r_ = (uint8_t)(30 + u*(235-30)); g_ = (uint8_t)(200 + u*(170-200)); b_ = (uint8_t)(40*(1.0f-u)); }
      else { float u = (tb_bar_norm - 0.5f) / 0.5f;
        r_ = 235; g_ = (uint8_t)(170 + u*(30-170)); b_ = 0; }
      if (fw > 0) tb_canvas.fillRect(0, by, fw, TB_BAR_H, tb565(r_, g_, b_));
    }
  }

#if BOARD_MODEL == BOARD_HELTEC_T114
  display.blit565(tb_canvas.getBuffer(), TB_W, TB_H);
#else
  display.startWrite();
  display.setAddrWindow(0, 0, TB_W, TB_H);
  display.writePixels(tb_canvas.getBuffer(), (uint32_t)TB_W * TB_H, true, false);
  display.endWrite();
#endif
}
#endif
