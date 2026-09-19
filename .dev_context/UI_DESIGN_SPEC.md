# UI Design Specification — LFPGo400 Battery Dashboard V3

**Author:** AGENT C — UI/UX AND MOTION DESIGN ARCHITECT  
**Target:** Waveshare ESP32-S3 Touch LCD 2.8 (240×320 portrait)  
**Framework:** LVGL 8.3.9  
**Date:** 2026-09-19  

---

## 1. DESIGN SYSTEM — THEME TOKENS

### 1.1 Color Palette

| Token Name | Hex | Usage |
| :--- | :--- | :--- |
| `BG_PRIMARY` | `#0B0E17` | Screen background — deep navy-black |
| `BG_CARD` | `#151A2A` | Card/panel background |
| `BG_CARD_ELEVATED` | `#1C2338` | Elevated card on interaction |
| `ACCENT_GOLD` | `#FFD700` | SOC gauge indicator, primary accent |
| `ACCENT_CYAN` | `#00E5FF` | Voltage readouts, secondary accent |
| `ACCENT_GREEN` | `#00E676` | Charging state, healthy cells |
| `ACCENT_ORANGE` | `#FF9100` | Discharging state, medium warning |
| `ACCENT_RED` | `#FF1744` | Fault/alarm, critical cells |
| `TEXT_PRIMARY` | `#FFFFFF` | Primary labels and values |
| `TEXT_SECONDARY` | `#8892A8` | Subtitle text, units, timestamps |
| `TEXT_DISABLED` | `#3A4052` | Unavailable data placeholder |
| `DIVIDER` | `#1E2640` | Section dividers, subtle borders |
| `GAUGE_TRACK` | `#1A2236` | SOC arc background track |
| `CELL_OK` | `#00E676` | Cell within healthy range |
| `CELL_WARN` | `#FFD700` | Cell approaching imbalance |
| `CELL_CRITICAL` | `#FF1744` | Cell outside safe range |

### 1.2 Typography Hierarchy

| Role | Font | Size | Color Token | Usage |
| :--- | :--- | :--- | :--- | :--- |
| **SOC Primary** | `lv_font_montserrat_32` | 32px | `TEXT_PRIMARY` | SOC percentage inside the gauge ring |
| **SOC Unit** | `lv_font_montserrat_14` | 14px | `TEXT_SECONDARY` | "%" suffix below SOC value |
| **Data Value** | `lv_font_montserrat_24` | 24px | `ACCENT_CYAN` or `ACCENT_GREEN` | Pack voltage, current |
| **Data Unit** | `lv_font_montserrat_14` | 14px | `TEXT_SECONDARY` | "V", "A", "W", "°C" units |
| **Card Title** | `lv_font_montserrat_14` | 14px | `TEXT_SECONDARY` | Section headers like "PACK VOLTAGE" |
| **Body / Label** | `lv_font_montserrat_14` | 14px | `TEXT_PRIMARY` | General labels |
| **Cell Value** | `lv_font_montserrat_16` | 16px | `TEXT_PRIMARY` | Individual cell mV readings |
| **Screen Title** | `lv_font_montserrat_20` | 20px | `TEXT_PRIMARY` | Screen headers |
| **Status Badge** | `lv_font_montserrat_14` | 14px | (dynamic) | "CHARGING" / "DISCHARGING" / "IDLE" |
| **Tiny / Caption** | `lv_font_montserrat_12` | 12px | `TEXT_SECONDARY` | Small annotations, timestamps |

### 1.3 Spacing & Layout Grid

| Token | Value | Usage |
| :--- | :--- | :--- |
| `MARGIN_SCREEN` | 12px | Left/right margin from screen edges |
| `MARGIN_TOP` | 8px | Top margin below status bar |
| `PAD_CARD` | 10px | Inner padding of card containers |
| `PAD_BETWEEN` | 8px | Vertical space between cards |
| `RADIUS_CARD` | 8px | Card corner radius |
| `RADIUS_SMALL` | 4px | Small element radius (pills, bars) |
| `RADIUS_GAUGE` | Full circle | SOC ring gauge |
| `TOUCH_MIN` | 44px | Minimum touch target dimension |
| `DIVIDER_H` | 1px | Horizontal divider thickness |

---

## 2. SCREEN ARCHITECTURE — THREE SCREENS

Navigation: Horizontal swipe between screens using LVGL native `LV_EVENT_GESTURE` (`LV_DIR_LEFT` / `LV_DIR_RIGHT`). A three-dot page indicator at the bottom shows current position. Transition animation: `LV_SCR_LOAD_ANIM_MOVE_LEFT` / `MOVE_RIGHT`, 250ms, `LV_ANIM_PATH_EASE_OUT`.

### Screen Flow:

```
 ← swipe left                                   swipe right →
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  SCREEN 1    │ ←→ │  SCREEN 2    │ ←→ │  SCREEN 3    │
│  DASHBOARD   │    │  CELLS       │    │  HEALTH      │
└──────────────┘    └──────────────┘    └──────────────┘
```

---

## 3. SCREEN 1 — MAIN DASHBOARD (240×320)

### 3.1 Layout Wireframe (top to bottom)

```
┌──────────────────────────────────┐ y=0
│  STATUS BAR (12px pad, 24px h)   │ y=0..24
│  ● Connected    LFPGo400  31°C  │
├──────────────────────────────────┤ y=24
│                                  │
│       ┌──────────────────┐       │
│       │   ╭────────────╮ │       │ y=36..196
│       │   │            │ │       │   SOC GAUGE
│       │   │    85%     │ │       │   160×160px
│       │   │            │ │       │   centered at x=120
│       │   ╰────────────╯ │       │   top at y=36
│       └──────────────────┘       │
│                                  │
│    [CHARGING] or [DISCHARGING]   │ y=200..224
│          status pill             │   centered, 24px h
├──────────────────────────────────┤ y=232
│  ┌─────────┐    ┌─────────┐     │
│  │ 26.4    │    │ +12.3   │     │ y=232..290
│  │ VOLTS   │    │ AMPS    │     │   Two data cards
│  └─────────┘    └─────────┘     │   each ~104×58px
├──────────────────────────────────┤ y=298
│         ● ○ ○  (page dots)      │ y=302..316
│              12px bottom pad     │
└──────────────────────────────────┘ y=320
```

### 3.2 Component Specifications

#### Status Bar (y=0..24)
- **Height:** 24px
- **Background:** `BG_PRIMARY` (flush with screen)
- **Left:** Connection indicator dot (8×8px circle). Green (`ACCENT_GREEN`) when connected, pulsing red (`ACCENT_RED`) when disconnected.
- **Center:** Device name "LFPGo400" in `montserrat_12`, `TEXT_SECONDARY`.
- **Right:** Temperature reading "31°C" in `montserrat_12`, `TEXT_SECONDARY`. Color shifts to `ACCENT_ORANGE` above 40°C, `ACCENT_RED` above 50°C.

#### SOC Ring Gauge (y=36..196)
- **Widget:** `lv_arc` (non-clickable, knob removed)
- **Outer Size:** 160×160px, centered horizontally (x=40..200)
- **Arc Width:** 14px indicator, 14px track
- **Track Color:** `GAUGE_TRACK` (`#1A2236`)
- **Indicator Color:** Dynamic by SOC:
  - SOC ≥ 50%: `ACCENT_GREEN` (`#00E676`)
  - SOC 20–49%: `ACCENT_GOLD` (`#FFD700`)
  - SOC < 20%: `ACCENT_RED` (`#FF1744`)
- **Arc Angles:** Start at 135° (bottom-left), sweep 270° clockwise (ending bottom-right). Value 0 = empty, 100 = full sweep.
- **Center Text:** SOC percentage in `montserrat_32`, `TEXT_PRIMARY`, vertically centered inside the ring.
- **Sub-text:** "%" in `montserrat_14`, `TEXT_SECONDARY`, 4px below the number.

#### Status Pill (y=200..224)
- **Widget:** `lv_obj` styled as rounded pill (RADIUS=12px)
- **Size:** Auto-width (text + 24px horizontal padding) × 24px
- **Alignment:** Centered horizontally
- **States:**
  - Charging (current > 0): Background `ACCENT_GREEN` 20% opacity, text `ACCENT_GREEN`, label "⚡ CHARGING"
  - Discharging (current < 0): Background `ACCENT_ORANGE` 20% opacity, text `ACCENT_ORANGE`, label "▼ DISCHARGING"
  - Idle (current ≈ 0): Background `DIVIDER`, text `TEXT_SECONDARY`, label "— IDLE"
  - Disconnected: Background `ACCENT_RED` 20% opacity, text `ACCENT_RED`, label "✕ DISCONNECTED"
- **Font:** `montserrat_14`

#### Data Cards Row (y=232..290)
- **Layout:** Two cards side by side with 8px gap between them.
- **Each Card Size:** 104×58px (total width: 104 + 8 + 104 = 216px, centered with 12px margins)
- **Card Background:** `BG_CARD` with `RADIUS_CARD` (8px)
- **Card Contents:**
  - **Top:** Value in `montserrat_24`. Left card: voltage in `ACCENT_CYAN`. Right card: current in dynamic color (green for charging, orange for discharging).
  - **Bottom:** Label in `montserrat_12`, `TEXT_SECONDARY`. Left: "VOLTS". Right: "AMPS".
  - **Inner Padding:** 10px all sides

#### Page Indicator (y=302..316)
- **Widget:** Three `lv_obj` circles, 8×8px each, 8px spacing
- **Active:** `ACCENT_GOLD`, filled
- **Inactive:** `DIVIDER`, border-only
- **Alignment:** Centered horizontally

---

## 4. SCREEN 2 — CELL DIAGNOSTICS (240×320)

### 4.1 Layout Wireframe

```
┌──────────────────────────────────┐ y=0
│  CELL VOLTAGES        Δ 12mV    │ y=0..32
│  screen title    max delta      │   title bar
├──────────────────────────────────┤ y=36
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ │
│ │ C1   │ │ C2   │ │ C3   │ │ C4   │ │ y=36..112
│ │3342mV│ │3338mV│ │3340mV│ │3345mV│ │   Row 1
│ │ ████ │ │ ████ │ │ ████ │ │ ████ │ │
│ └──────┘ └──────┘ └──────┘ └──────┘ │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ │
│ │ C5   │ │ C6   │ │ C7   │ │ C8   │ │ y=120..196
│ │3341mV│ │3339mV│ │3342mV│ │3340mV│ │   Row 2
│ │ ████ │ │ ████ │ │ ████ │ │ ████ │ │
│ └──────┘ └──────┘ └──────┘ └──────┘ │
├──────────────────────────────────┤ y=204
│  MIN: 3338mV (C2)               │ y=204..240
│  MAX: 3345mV (C4)               │   Summary card
│  DELTA: 7mV                     │
├──────────────────────────────────┤ y=248
│  (rows 3-4 if cellCount > 8)    │ y=248..296
│  C9..C16 — same grid layout     │   conditional
├──────────────────────────────────┤ y=302
│         ○ ● ○  (page dots)      │
└──────────────────────────────────┘
```

### 4.2 Component Specifications

#### Title Bar (y=0..32)
- **Left:** "CELL VOLTAGES" in `montserrat_20`, `TEXT_PRIMARY`
- **Right:** "Δ 12mV" showing max-min cell delta, in `montserrat_14`. Color: `CELL_OK` if delta ≤ 20mV, `CELL_WARN` if 20–50mV, `CELL_CRITICAL` if > 50mV.

#### Cell Voltage Cards (4-column grid)
- **Grid:** 4 columns × 2 rows minimum (4 rows max for 16 cells)
- **Each Card Size:** 52×72px (4 × 52 + 3 × 4px gap + 2 × 12px margin = 232px ≈ 240)
- **Card Background:** `BG_CARD`, `RADIUS_SMALL` (4px)
- **Card Contents (top to bottom):**
  - Cell label "C1" in `montserrat_12`, `TEXT_SECONDARY` (4px top pad)
  - Voltage "3342" in `montserrat_16`, `TEXT_PRIMARY` (centered)
  - Mini progress bar (44×6px): fill % = `(mV - 2800) / (3650 - 2800) * 100`, color = dynamic by cell health status
- **Cell Health Color Logic:**
  - Cell is the minimum voltage cell: bar color = `CELL_WARN`
  - Cell is the maximum voltage cell: bar color = `ACCENT_CYAN`
  - Cell voltage < 2900mV: bar and text color = `CELL_CRITICAL`
  - All others: bar color = `CELL_OK`

#### Summary Card (y=204..240)
- **Size:** 216×36px, centered
- **Background:** `BG_CARD`, `RADIUS_CARD`
- **Contents:** Three rows of key-value pairs in `montserrat_12`:
  - "MIN: 3338mV (C2)" — text color `CELL_WARN`
  - "MAX: 3345mV (C4)" — text color `ACCENT_CYAN`
  - "DELTA: 7mV" — text color dynamically matched to delta severity

---

## 5. SCREEN 3 — SYSTEM HEALTH & ALARMS (240×320)

### 5.1 Layout Wireframe

```
┌──────────────────────────────────┐ y=0
│  SYSTEM HEALTH                   │ y=0..32
├──────────────────────────────────┤ y=36
│  ┌────────────────────────────┐  │
│  │ CAPACITY                   │  │ y=36..100
│  │ 95.2 / 100.0 Ah           │  │
│  │ ████████████████████░░░░░  │  │   capacity bar
│  └────────────────────────────┘  │
│  ┌─────────┐    ┌─────────┐     │
│  │ CYCLES  │    │ TEMP    │     │ y=108..170
│  │  142    │    │  31°C   │     │   two metric cards
│  └─────────┘    └─────────┘     │
├──────────────────────────────────┤ y=178
│  PROTECTION STATUS               │ y=178..200
├──────────────────────────────────┤
│  ┌──┐ ┌──┐ ┌──┐ ┌──┐ ┌──┐      │ y=204..260
│  │OV│ │UV│ │OC│ │SC│ │OT│      │   alarm indicator
│  │OK│ │OK│ │OK│ │OK│ │OK│      │   grid
│  └──┘ └──┘ └──┘ └──┘ └──┘      │
├──────────────────────────────────┤ y=268
│  FET STATUS                      │
│  CHG: ON    DCHG: ON            │ y=268..296
├──────────────────────────────────┤ y=302
│         ○ ○ ●  (page dots)      │
└──────────────────────────────────┘
```

### 5.2 Component Specifications

#### Capacity Card (y=36..100)
- **Size:** 216×64px, centered
- **Background:** `BG_CARD`, `RADIUS_CARD`
- **Top Row:** "CAPACITY" in `montserrat_14`, `TEXT_SECONDARY`
- **Middle Row:** "95.2 / 100.0 Ah" — remaining in `montserrat_24`, `ACCENT_CYAN`; " / 100.0 Ah" in `montserrat_14`, `TEXT_SECONDARY`
- **Bottom Row:** `lv_bar` (196×8px), range 0–100, value = `(remainingAh / designAh) * 100`. Bar color: green above 50%, gold 20–50%, red below 20%.

#### Metric Cards (y=108..170)
- Same visual style as Dashboard data cards (104×58px each, `BG_CARD`)
- **Left (Cycles):** Value in `montserrat_24`, `ACCENT_CYAN`. Label "CYCLES" in `montserrat_12`, `TEXT_SECONDARY`.
- **Right (Temperature):** Value in `montserrat_24`, dynamic color (green ≤ 35°C, orange 36–45°C, red > 45°C). Label "TEMP" in `montserrat_12`, `TEXT_SECONDARY`.

#### Protection Alarm Grid (y=204..260)
- **Layout:** 5 square indicators in a row (40×40px each, 6px gap)
- **Background:** `BG_CARD`, `RADIUS_SMALL`
- **Each Indicator:**
  - Top: Alarm abbreviation in `montserrat_12`, `TEXT_PRIMARY` ("OV", "UV", "OC", "SC", "OT")
  - Bottom: Status "OK" in `ACCENT_GREEN` or "TRIP" in `ACCENT_RED`
  - Background shifts to `ACCENT_RED` 15% opacity when tripped
- **Alarms mapped from protection bitmask:**
  - Bit 0: Cell Overvoltage (OV)
  - Bit 1: Cell Undervoltage (UV)
  - Bit 2: Pack Overcurrent Discharge (OC)
  - Bit 3: Short Circuit (SC)
  - Bit 4: Overtemperature (OT)

#### FET Status Row (y=268..296)
- **Size:** 216×28px, centered
- **Background:** `BG_CARD`, `RADIUS_CARD`
- **Contents:** "CHG:" + status dot (green/red) + "ON"/"OFF" | "DCHG:" + status dot + "ON"/"OFF"
- **Font:** `montserrat_14`

---

## 6. MOTION DESIGN SPECIFICATION

### 6.1 SOC Gauge Animated Update

| Property | Value |
| :--- | :--- |
| **Component** | SOC `lv_arc` indicator angles |
| **Trigger** | New telemetry snapshot with changed SOC value |
| **Initial State** | Current arc angle position |
| **Final State** | New arc angle position mapped from new SOC |
| **Duration** | 400ms |
| **Easing** | `LV_ANIM_PATH_EASE_OUT` |
| **Type** | Event-driven (only on SOC change) |
| **Fallback** | Instant snap if animation is disabled |
| **Performance** | Single arc redraw; LVGL handles dirty-rect optimization |

### 6.2 SOC Gauge Color Transition

| Property | Value |
| :--- | :--- |
| **Component** | SOC `lv_arc` indicator color |
| **Trigger** | SOC crosses threshold boundary (50%, 20%) |
| **Duration** | Instant color swap (no gradient transition to avoid complexity) |
| **Type** | Event-driven |

### 6.3 Screen Slide Transitions

| Property | Value |
| :--- | :--- |
| **Component** | Full screen objects |
| **Trigger** | Swipe gesture `LV_DIR_LEFT` or `LV_DIR_RIGHT` on any screen |
| **Initial State** | Current screen visible, next screen off-canvas |
| **Final State** | Next screen centered, current screen off-canvas |
| **Duration** | 250ms |
| **Easing** | `LV_ANIM_PATH_EASE_OUT` |
| **LVGL API** | `lv_scr_load_anim(next_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false)` |
| **Fallback** | Instant `lv_scr_load()` if animation causes frame drops |
| **Performance** | Uses LVGL built-in screen load animation; no custom rendering required |

### 6.4 Connection Status Pulse

| Property | Value |
| :--- | :--- |
| **Component** | Status bar connection indicator dot (8×8px circle) |
| **Trigger** | BLE disconnected state (`batteryConnected == false`) |
| **Animation** | Opacity cycles between `LV_OPA_30` and `LV_OPA_COVER` |
| **Duration** | 1000ms per cycle |
| **Easing** | `LV_ANIM_PATH_EASE_IN_OUT` |
| **Type** | Continuous while disconnected, stops on reconnect |
| **Fallback** | Static red dot if animation is disabled |
| **Performance** | Single 8×8px object opacity change; negligible cost |

### 6.5 Status Pill State Transition

| Property | Value |
| :--- | :--- |
| **Component** | Status pill background color and text |
| **Trigger** | Charging/discharging/idle state change |
| **Duration** | Instant swap (no animated color gradient) |
| **Type** | Event-driven |

### 6.6 Data Value Update

| Property | Value |
| :--- | :--- |
| **Component** | Voltage, current, temperature labels |
| **Trigger** | New telemetry snapshot |
| **Animation** | None (instant text update). Text change is fast enough at 4Hz that animation would cause visual noise. |
| **Fallback** | N/A |

### 6.7 Cell Bar Fill Animation

| Property | Value |
| :--- | :--- |
| **Component** | Cell voltage mini-bars on Screen 2 |
| **Trigger** | New cell voltage data |
| **Duration** | 300ms |
| **Easing** | `LV_ANIM_PATH_EASE_OUT` (via `LV_ANIM_ON` in `lv_bar_set_value`) |
| **Type** | Event-driven |
| **Performance** | Up to 16 small bars (44×6px each), minimal redraw cost |

### 6.8 Capacity Bar Animation

| Property | Value |
| :--- | :--- |
| **Component** | Capacity bar on Screen 3 |
| **Trigger** | New capacity data |
| **Duration** | 400ms |
| **Easing** | `LV_ANIM_PATH_EASE_OUT` |
| **Type** | Event-driven |

### 6.9 Touch Press Feedback

| Property | Value |
| :--- | :--- |
| **Component** | Data cards on Screens 1, 3 |
| **Trigger** | `LV_EVENT_PRESSED` on card |
| **Animation** | Background color shifts from `BG_CARD` → `BG_CARD_ELEVATED`, returns on `LV_EVENT_RELEASED` |
| **Duration** | 80ms (matches `LV_THEME_DEFAULT_TRANSITION_TIME`) |
| **Performance** | Single object background repaint |

---

## 7. DISCONNECTED / UNAVAILABLE-DATA STATES

When `batteryConnected == false` or `telemetry.timestampMs == 0`:

- SOC gauge: Arc set to 0%, color `GAUGE_TRACK`, center text shows "—" in `TEXT_DISABLED`
- Voltage/Current cards: Show "—.—" in `TEXT_DISABLED`
- Status pill: Shows "✕ DISCONNECTED" in `ACCENT_RED`
- Connection dot: Pulsing red (animation 6.4)
- Cell voltage screen: All cells show "—" with empty bars
- Health screen: All values show "—" in `TEXT_DISABLED`

When connected but waiting for first data (`telemetry.timestampMs > 0` but stale > 5s):
- Show "STALE" badge on status bar in `ACCENT_ORANGE`

---

## 8. GRAPHICS ASSET REQUIREMENTS

### 8.1 Asset Classification

| Asset | Type | Implementation | Notes |
| :--- | :--- | :--- | :--- |
| SOC Ring Gauge | Native LVGL | `lv_arc` widget | No raster asset needed |
| Connection Dot | Native LVGL | `lv_obj` with radius=4, background color | 8×8px programmatic circle |
| Status Pill | Native LVGL | `lv_obj` + `lv_label` | Styled rectangle with text |
| Data Cards | Native LVGL | `lv_obj` containers | Background color + radius |
| Cell Mini-Bars | Native LVGL | `lv_bar` widgets | 44×6px each |
| Capacity Bar | Native LVGL | `lv_bar` widget | 196×8px |
| Page Dots | Native LVGL | Three `lv_obj` circles | 8×8px each |
| Alarm Indicators | Native LVGL | `lv_obj` + `lv_label` grid | 40×40px cards with text |
| Icons (⚡▼—✕) | Unicode text | `lv_label` character | No image asset needed |

### 8.2 Conclusion on Raster/Image Assets

**No raster image assets are required for the initial implementation.** The entire design is achievable using native LVGL widgets, styled containers, and programmatic drawing. This is critical for:
- Minimizing flash footprint
- Avoiding image decode overhead
- Maintaining predictable rendering performance
- Simplifying the build pipeline

If the user later requests decorative artwork (e.g., themed splash screen, background pattern, custom icons), those can be added as optimized C arrays converted from small PNGs.

---

## 9. NAVIGATION IMPLEMENTATION PLAN

### Replace Current Transparent Buttons with Gesture Events

**Current Issue:** Two invisible 100×320px buttons consume 83% of the touch surface.

**New Approach:**
1. Register `LV_EVENT_GESTURE` callback on each screen's root object.
2. On `LV_DIR_LEFT` gesture → load next screen with `LV_SCR_LOAD_ANIM_MOVE_LEFT`.
3. On `LV_DIR_RIGHT` gesture → load previous screen with `LV_SCR_LOAD_ANIM_MOVE_RIGHT`.
4. Update page indicator dots on `LV_EVENT_SCREEN_LOADED`.
5. No invisible buttons needed.

**Fallback:** If gesture detection is unreliable on the CST328 (suspected touch invalidation issue), implement touch-zone tap navigation as a secondary option (tap bottom-left dot to go left, bottom-right to go right) with 44×44px minimum touch targets.

---

## 10. PERFORMANCE BUDGET

| Resource | Budget | Notes |
| :--- | :--- | :--- |
| Flash (sketch) | ≤ 1.5 MB of 3 MB partition | Currently 919 KB (29%). New fonts add ~60KB. |
| SRAM (dynamic) | ≤ 220 KB of 327 KB | Currently 187 KB (57%). 128KB LVGL pool included. |
| Draw buffer | 15.36 KB (two 7.68KB buffers) | Adequate for partial refresh |
| Max animations per frame | 3 concurrent | SOC arc + 1 bar + 1 screen transition |
| Target refresh rate | 30 FPS (33ms tick) | `LV_DISP_DEF_REFR_PERIOD = 30` |
| Touch read rate | 30 Hz | `LV_INDEV_DEF_READ_PERIOD = 30` |

**Optimization note:** `disp_drv.full_refresh` should be changed from `1` to `0` during implementation to enable dirty-rectangle partial updates, significantly reducing SPI bus traffic.

---

## 11. HANDOFF TO AGENT C2 AND AGENT D

### For Agent C2 (Graphics and Asset Specialist):
No raster assets are required for the initial implementation. The entire design uses native LVGL widgets. Agent C2 should review this document and confirm no graphical gaps. If decorative assets are later requested, Agent C2 will produce conversion-ready C arrays.

### For Agent D (Embedded LVGL Engineer):
1. Implement the three screens as described in sections 3, 4, 5.
2. Use the exact color tokens, font assignments, and dimensions specified.
3. Implement gesture navigation as described in section 9.
4. Implement animations as described in section 6.
5. Implement disconnected states as described in section 7.
6. Change `full_refresh` to `0` in `LVGL_Driver.cpp`.
7. Do not modify the protected BLE/BMS subsystems.
