# Baseline Project & UI Architecture Report

## A. Current Architecture Report
- **Hardware:** ESP32-S3 (Dual-Core, 8MB PSRAM, 16MB Flash) driving a Waveshare 2.8" ST7789 IPS Touch Display (240x320 portrait).
- **Communication:** Passive NimBLE client connecting to the JBD BMS (`CE024AA005260138`).
- **Telemetry Layer:** The BMS data is polled alternatingly (`0x03` basic info, `0x04` cell voltages) via FreeRTOS `logicTask` running on Core 0. Data is shared via a Mutex-protected `Telemetry` struct.
- **Rendering Layer:** LVGL 8.x is integrated, running in `uiTask` on Core 1 (`Lvgl_Loop()`). The display is driven by a custom DMA SPI driver (`Display_ST7789`), and touch input is processed via `Touch_CST328`.
- **Current UI:** Two basic screens. `main_scr` with a Pacman-style `lv_arc` representing SOC, and text labels for voltage and current. `cells_scr` with 8 `lv_bar` widgets for cell voltages. Navigation uses transparent side buttons instead of proper LVGL gesture events.

## B. Existing UI Problems
- **Resolution Mismatch:** The UI components are sparse and do not utilize the 240x320 real estate effectively. It is essentially a scaled-up low-res design.
- **Typography Limitations:** Currently, all text is constrained to `lv_font_montserrat_14` because larger fonts are disabled in `lv_conf.h`. This makes readability poor on a 2.8" screen.
- **Touch Navigation Hack:** Using massive invisible buttons (`LV_OPA_TRANSP`) over the edges blocks other potential interactions and isn't standard LVGL gesture handling.
- **Data Truncation:** Only 8 cell voltages are currently rendered due to vertical spacing limits in the hardcoded layout, even though the battery can have up to 16.

## C. UI Rendering and LVGL Risks
- **LVGL Configuration:** Expanding fonts requires safely modifying `lv_conf.h` inside the external library directory without breaking the build.
- **Thread Safety:** While `telemetryMutex` protects the raw data, LVGL API calls must only be made from the `uiTask` thread. Care must be taken not to trigger LVGL updates directly from BLE callbacks.
- **Memory:** The `huge_app` partition scheme is currently required. As we add high-res design assets (images, fonts, complex widgets), we must monitor flash and PSRAM usage to prevent out-of-memory crashes.

## D. Recommended Multi-Agent Task Breakdown
1. **AGENT B (Analyst):** Audit `lv_conf.h` and the DMA display buffer memory allocation to ensure we have the overhead for a high-res design system.
2. **AGENT C (Design Architect):** Draft the visual design tokens (colors, typography, grid spacing) matching the Schematik reference, tailored strictly for 240x320 portrait.
3. **AGENT D (LVGL Engineer):** Implement reusable LVGL widgets (e.g., custom SOC battery gauge, polished cell voltage cards) and replace the transparent-button navigation with native screen swipe gestures.
4. **AGENT E (Firmware Reviewer):** Ensure the BLE `logicTask` remains untouched and that the `uiTask` consumes the `Telemetry` struct safely.
5. **AGENT F (QA):** Verify layout alignments, check for memory leaks, and validate swipe fluidity.

## E. Proposed Professional UI Architecture
- **Screen 1 (Main Dashboard):** Dominant circular or pill-shaped SOC gauge. Large primary typography for Pack Voltage and Current. Status badge (Charging/Discharging/Idle).
- **Screen 2 (Cell Diagnostics):** A scrollable list or a dense grid of 16 cell voltage indicators showing min/max cell delta to highlight pack health.
- **Screen 3 (System Health):** Cycle count, temperature probes, and design vs. remaining capacity.
- **Navigation:** Native LVGL left/right touch swipe gestures (using `lv_obj_add_event_cb` with `LV_EVENT_GESTURE`) rather than invisible buttons.

## F. Design System Specification (Draft)
- **Backgrounds:** Deep slate/black (`#0F111A` or `#000000`) for OLED-like contrast.
- **Primary Accents:** Clean Electric Brand Colors / Pacman Theme (Yellow/Gold `#FFD700`, Cyan `#00E5FF`).
- **Status Colors:** Charging (Green `#00E676`), Discharging (Orange `#FF9100`), Fault (Red `#FF1744`).
- **Typography Hierarchy:**
  - *Header:* 24px (requires enabling in `lv_conf.h`)
  - *Data Primary:* 32px or custom symbol for SOC/Voltage.
  - *Body/Labels:* 14px (current default).
- **Component Padding:** Standardized 10px margins, 8px inner padding for cards/panels.

## G. Implementation Roadmap
1. **Approval Phase:** Await user sign-off on this baseline report.
2. **Config Phase:** Enable `LV_FONT_MONTSERRAT_24` and `LV_FONT_MONTSERRAT_32` in `lv_conf.h`.
3. **Foundation Phase:** Abstract the LVGL screen initialization in `v3.ino` into clean separate functions (`build_main_screen`, `build_cell_screen`).
4. **Component Phase:** Build and polish the custom UI widgets using the new typography and color tokens.
5. **Validation Phase:** Compile, upload, and review on physical hardware.
