# Asset Manifest — LFPGo400 Battery Dashboard V3

**Author:** AGENT C2 — GRAPHICS AND ASSET SPECIALIST  
**Date:** 2026-09-19  

---

## 1. ASSET CLASSIFICATION SUMMARY

After reviewing the UI Design Specification (Agent C) and auditing the hardware constraints, **all UI components for the three operational screens will be implemented using native LVGL widgets and programmatic drawing.** No raster image assets are required for the core dashboard.

**Rationale:**
- A single full-screen RGB565 image costs 150 KB of flash — over 15% of available headroom.
- Every UI element in the design spec (arcs, bars, labels, styled containers) maps directly to an LVGL 8.x widget.
- Native widgets support animated property changes (color, size, value) for free.
- Eliminating image decoding removes latency spikes and simplifies the build pipeline.

---

## 2. COMPLETE ASSET REGISTRY

### 2.1 Native LVGL Components (No File Assets)

| Asset ID | Component | Widget Type | Screen | Dimensions | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `NV-01` | SOC Ring Gauge | `lv_arc` | Screen 1 | 160×160px | Non-clickable, knob removed, 14px arc width |
| `NV-02` | SOC Percentage Text | `lv_label` | Screen 1 | Auto | `montserrat_32`, centered inside arc |
| `NV-03` | Connection Indicator | `lv_obj` | Screen 1 | 8×8px | Circular (radius=4), green/red background |
| `NV-04` | Status Pill | `lv_obj` + `lv_label` | Screen 1 | Auto×24px | Rounded rect (radius=12), semi-transparent bg |
| `NV-05` | Voltage Card | `lv_obj` + `lv_label` ×2 | Screen 1 | 104×58px | `BG_CARD`, `RADIUS_CARD` |
| `NV-06` | Current Card | `lv_obj` + `lv_label` ×2 | Screen 1 | 104×58px | `BG_CARD`, `RADIUS_CARD` |
| `NV-07` | Page Indicator Dots | `lv_obj` ×3 | All | 8×8px each | Circular, active=gold, inactive=dimmed |
| `NV-08` | Cell Voltage Card | `lv_obj` + `lv_label` ×2 + `lv_bar` | Screen 2 | 52×72px | 4×2 grid (up to 4×4 for 16 cells) |
| `NV-09` | Cell Mini-Bar | `lv_bar` | Screen 2 | 44×6px | Inside each cell card, dynamic color |
| `NV-10` | Cell Summary Card | `lv_obj` + `lv_label` ×3 | Screen 2 | 216×36px | MIN/MAX/DELTA readouts |
| `NV-11` | Capacity Bar | `lv_bar` | Screen 3 | 196×8px | Green→gold→red by percentage |
| `NV-12` | Cycles Card | `lv_obj` + `lv_label` ×2 | Screen 3 | 104×58px | Same visual style as voltage/current cards |
| `NV-13` | Temperature Card | `lv_obj` + `lv_label` ×2 | Screen 3 | 104×58px | Dynamic color by temperature |
| `NV-14` | Alarm Indicator | `lv_obj` + `lv_label` ×2 | Screen 3 | 40×40px | 5 indicators in a row (OV/UV/OC/SC/OT) |
| `NV-15` | FET Status Card | `lv_obj` + `lv_label` | Screen 3 | 216×28px | CHG/DCHG status with dot indicators |

### 2.2 Programmatic Animations (No File Assets)

| Asset ID | Animation | LVGL API | Screen | Trigger | Duration |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `AN-01` | SOC arc value sweep | `lv_anim_t` on arc angles | Screen 1 | SOC data change | 400ms |
| `AN-02` | Screen slide transition | `lv_scr_load_anim()` | All | Swipe gesture | 250ms |
| `AN-03` | Connection dot pulse | `lv_anim_t` on `LV_STYLE_OPA` | Screen 1 | BLE disconnected | 1000ms cycle |
| `AN-04` | Cell bar fill | `lv_bar_set_value(..., LV_ANIM_ON)` | Screen 2 | Cell data update | 300ms |
| `AN-05` | Capacity bar fill | `lv_bar_set_value(..., LV_ANIM_ON)` | Screen 3 | Capacity update | 400ms |
| `AN-06` | Touch card press | `LV_STYLE_TRANSITION` bg color | All cards | Touch press/release | 80ms |

### 2.3 Boot/Splash Screen — PROGRAMMATIC (Not Raster)

| Asset ID | Component | Implementation | Screen | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `SP-01` | Boot splash screen | **Native LVGL widgets** | Boot (Screen 0) | See section 3 below |

**Decision:** The boot splash screen will be built with native LVGL objects rather than a raster image.

**Cost comparison:**
- Raster approach: 240×320 RGB565 = **153,600 bytes** of flash. Requires image conversion tooling. No animation possible without additional frames.
- Programmatic approach: ~50 lines of LVGL C code, **<500 bytes** of flash. Supports an animated loading bar and pulsing text. Zero image decode latency.

---

## 3. BOOT SPLASH SCREEN SPECIFICATION (SP-01)

**Purpose:** Shown during the 1-6 seconds while the ESP32-S3 actively scans for the JBD BMS over BLE. Replaced by Screen 1 (Dashboard) once `batteryConnected == true`.

**AI-generated reference image:** The splash screen mockup generated earlier in this session serves as the visual target. The actual implementation is programmatic.

### Layout (centered on 240×320, `BG_PRIMARY` background)

```
y=100:  Battery outline icon — drawn with lv_obj (rect 40×56px, 
        gold border 2px, small cap rect 12×6px on top)
        Centered at x=120

y=180:  "LFPGo400" label — montserrat_24, TEXT_PRIMARY
        Centered

y=220:  Loading bar — lv_bar, 120×4px, ACCENT_GREEN indicator
        Indeterminate: lv_anim_t sweeps value 0→100 continuously
        Duration: 2000ms per cycle, EASE_IN_OUT

y=280:  "SCANNING FOR BMS..." — montserrat_12, TEXT_SECONDARY
        Centered, opacity pulses 50%→100% (1500ms cycle)
```

### Animation Specifications

| Animation | API | Duration | Behavior |
| :--- | :--- | :--- | :--- |
| Loading bar sweep | `lv_anim_t` on `lv_bar` value | 2000ms per cycle | Continuous loop, `LV_ANIM_REPEAT_INFINITE` |
| "SCANNING..." pulse | `lv_anim_t` on `LV_STYLE_OPA` | 1500ms per cycle | Continuous, `EASE_IN_OUT`, stops on connect |

### Transition to Dashboard
When `batteryConnected` becomes `true`, load Screen 1 with `LV_SCR_LOAD_ANIM_FADE_IN` over 300ms.

---

## 4. UNICODE SYMBOL INVENTORY

The design uses text-based symbols instead of icon images. These must be validated against the Montserrat font's glyph coverage:

| Symbol | Unicode | Usage | Available in Montserrat? |
| :--- | :--- | :--- | :--- |
| ⚡ | U+26A1 | Charging status pill | **No** — use "+" prefix or LV_SYMBOL_CHARGE |
| ▼ | U+25BC | Discharging status pill | **No** — use "-" prefix |
| — | U+2014 | Unavailable data placeholder | **Yes** (em dash) |
| ✕ | U+2715 | Disconnected status pill | **No** — use "X" |
| ● | U+25CF | Page dot active | **No** — use `lv_obj` circle |
| ○ | U+25CB | Page dot inactive | **No** — use `lv_obj` circle |

**Resolution:** The Montserrat font in LVGL only includes ASCII + common Latin symbols. Special Unicode symbols (⚡▼✕●○) are **not available**. Agent D must use:
- Text prefixes: "+" CHARGING, "-" DISCHARGING, "X DISCONNECTED", "- IDLE"
- LVGL built-in symbols where available: `LV_SYMBOL_CHARGE`, `LV_SYMBOL_WARNING`
- Programmatic `lv_obj` circles for page dots (already specified in NV-07)

---

## 5. FUTURE ASSET PIPELINE (Not Required Now)

If decorative raster assets are ever requested, the conversion pipeline is:

1. **Source:** PNG file, RGB565 or ARGB8565, dimensions ≤ 120×120px for icons.
2. **Converter:** LVGL Online Image Converter (https://lvgl.io/tools/imageconverter) → output as C array.
3. **Format:** `LV_IMG_CF_TRUE_COLOR` (RGB565, no alpha) or `LV_IMG_CF_TRUE_COLOR_ALPHA` (ARGB8565).
4. **Integration:** Include the C array header in the sketch, use `lv_img_create()` + `lv_img_set_src()`.
5. **Budget:** Each 60×60 RGB565 icon = 7,200 bytes. Maximum recommended: 5-10 small icons (~50 KB total).

---

## 6. AGENT C2 FINDINGS AND HANDOFF

### Files Inspected
- `.dev_context/UI_DESIGN_SPEC.md` — complete design specification
- `.dev_context/FIRMWARE_AND_ARCHITECTURE_REPORT.md` — hardware constraints
- `v3/` directory — no existing asset files
- `esp32 2.8_offical_example_library/Arduino/libraries/lvgl/src/lv_conf.h` — font and image configuration

### Findings
1. **Zero raster assets required.** Every UI element maps to a native LVGL widget.
2. **Boot splash is best built programmatically** — saves 150 KB of flash, enables animated loading bar.
3. **Unicode symbols in the design spec are unavailable** in the compiled Montserrat fonts. Text prefixes and LVGL built-in symbols must be used instead.
4. **No asset conversion tooling is needed** for the initial implementation.

### Recommended Actions for Agent D
1. Build the boot splash (SP-01) as a 4th LVGL screen loaded at startup, dismissed on BLE connect.
2. Use text-based status labels ("+CHARGING", "-DISCHARGING") instead of Unicode symbols.
3. Use `lv_obj` circles for page indicator dots, not font characters.
4. All animations use `lv_anim_t` or built-in `LV_ANIM_ON` — no pre-rendered frame sequences.

### Remaining Issues
- None. No assets are blocked or missing.
