# Project Implementation Roadmap & Multi-Agent Sequence

## 1. Project Goal & UI Architecture
To build a high-resolution, interactive embedded dashboard for the LFPGo400 battery on the Waveshare ESP32-S3 Touch LCD 2.8. 
This UI will not be a static layout, but an embedded product-quality visual system incorporating custom LVGL UI components, programmatic animations, screen transitions, AI-generated vector/raster graphics, and data-driven visual states—all while preserving the hardware telemetry and BLE communication logic.

## 2. Protected Subsystems
The following subsystems are strictly protected and must not be altered for UI convenience:
- JBD BMS BLE protocol parsing (Payloads `0x03` and `0x04`).
- `logicTask` running on Core 0.
- `Telemetry` synchronization mutex.
- ESP32-S3 hardware pin assignments and DMA SPI drivers.

## 3. Updated Multi-Agent Sequence

### Agent A — Master Orchestrator (Current)
- Coordinates all specialized agents sequentially.
- Prevents conflicting modifications and enforces hardware limits.
- Maintains Git continuity, developer handoffs, and asset manifests.

### Agent B — Repository and Firmware Analyst (Completed)
- Audited repository configurations (`lv_conf.h`), enabled necessary fonts, increased `LV_MEM_SIZE` to 128KB, and confirmed firmware stability and telemetry data availability.

### Agent C — UI/UX and Motion Design Architect (Next)
- Produces the **Implementation-Ready Design Specification**.
- Defines the 240x320 portrait grid, color tokens, typography hierarchy, component dimensions, and touch navigation.
- Specifies the **Animation and Transition System** (trigger conditions, durations, easing, fallbacks) for SOC gauges, screen slides, and status badges.
- Must deliver concrete specifications, wireframes, or reference mocks, not generic descriptions.

### Agent C2 — Graphics and Asset Specialist
- Works alongside the design spec to generate, optimize, and convert visual assets (custom graphics, loading animations, background concepts).
- Decides whether an asset should be drawn natively in LVGL, embedded as an optimized raster (PNG), or programmatically animated.
- Validates dimensions, flash memory cost, color depth, and transparency.
- Maintains the `ASSET_MANIFEST.md` detailing the source, format, and memory footprint of every graphic.

### Agent D — Embedded LVGL Engineer
- Implements the UI strictly according to Agent C and C2's approved design specifications.
- Utilizes the LVGL 8.x C API to build reusable components, attach gesture navigation, and implement event-driven (not blocking) animations.

### Agent E — Performance and Firmware Reviewer
- Audits Agent D's implementation to ensure no blocking UI loops stall the `logicTask`.
- Reviews PSRAM usage, LVGL draw buffer flush rates, animation overhead, and flash footprint.
- Ensures the UI changes do not silently compromise the `Telemetry` struct or BLE connection stability.

### Agent F — Visual and Hardware QA Reviewer
- Validates the final compiled binary against the design specification.
- Confirms touch responsiveness, rendering layout, and animation fluidity.
- Reviews hardware deployment status and visual regressions.

## 4. Persistent Asset Management
Any visual assets created by Agent C2 will be documented in a new `.dev_context/ASSET_MANIFEST.md` to track memory impact and format requirements.
