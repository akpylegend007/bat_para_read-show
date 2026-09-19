# Comprehensive Repository and Firmware Architecture Report

**Author:** AGENT B — REPOSITORY AND FIRMWARE ANALYST  
**Project:** LFPGo400 Battery Monitoring Dashboard (V3 LVGL)  
**Target Hardware:** Waveshare ESP32-S3 Touch LCD 2.8 (240x320 Portrait)  
**Date:** 2026-09-19  

---

## 1. Executive Summary & Architecture Map

The project is an embedded, real-time battery monitoring dashboard running on an **ESP32-S3** with an **ST7789 IPS LCD (240x320)** and **CST328 capacitive touch controller**. It interfaces as a passive BLE client with a **Jiabaida (JBD) Smart BMS** inside a Clean Electric LFPGo400 LiFePO4 battery pack.

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                             ESP32-S3 DUAL CORE                              │
├──────────────────────────────────────┬──────────────────────────────────────┤
│               CORE 0                 │                CORE 1                │
│             logicTask                │                uiTask                │
│         (Priority 1, 8KB)            │          (Priority 1, 16KB)          │
├──────────────────────────────────────┼──────────────────────────────────────┤
│  • NimBLE Active Scan & Connect      │  • Lvgl_Loop() (lv_timer_handler)    │
│  • Alternating Polling (4Hz):        │  • snapshotTelemetry()               │
│      - 0x03 Basic Info               │  • updateUI() data binding           │
│      - 0x04 Cell Voltages (1-16)     │  • ST7789 DMA Display Flush          │
│  • Packet Reassembly & JBD Parsing   │  • CST328 Touch Input Ingest         │
└──────────────────┬───────────────────┴──────────────────┬───────────────────┘
                   │                                      │
                   └──────────► telemetryMutex ◄──────────┘
```

---

## 2. Hardware Subsystems & Pin Assignments

| Peripheral | Controller | Interface / Bus | Pins / Channels | Configuration Notes |
| :--- | :--- | :--- | :--- | :--- |
| **SoC** | ESP32-S3R8 | Internal | 240MHz, 8MB PSRAM, 16MB Flash | `PartitionScheme=huge_app` (3MB App Partition) |
| **Display** | ST7789 IPS | FSPI (Hardware SPI) | SCLK=40, MOSI=45, CS=42, DC=41, RST=39 | 80MHz SPI Clock (`SPI_MODE0`), 240x320 Portrait |
| **Backlight** | LED Driver | LEDC PWM | GPIO 5 | 20kHz, 10-bit PWM (`Set_Backlight(50)` = 50% Duty for thermal cooling) |
| **Touch** | CST328 | Dedicated `Wire1` (I2C) | SDA=1, SCL=3, INT=4, RST=2 | Address `0x1A`, 400kHz clock, RISING interrupt on GPIO 4 |
| **Power Latch**| System Power | GPIO Output | GPIO 7 (`PWR_Control_PIN`) | **CRITICAL:** Must assert `HIGH` at boot to latch power. Driving `LOW` shuts down board. |
| **Power Button**| System Input | GPIO Input | GPIO 6 (`PWR_KEY_Input_PIN`) | Active Low button for sleep / power management |

---

## 3. Display, Memory, and LVGL Driver Architecture

### Display Rendering Pipeline
- **Driver:** `Display_ST7789.cpp` / `LVGL_Driver.cpp`.
- **Display Flush:** `Lvgl_Display_LCD()` passes dirty window bounding box (`area->x1, y1, x2, y2`) to `LCD_addWindow()`, which streams pixels over FSPI via `LCDspi.transferBytes()`.
- **Draw Buffers:** Two static buffers `buf1` and `buf2` of size `(240 * 320 / 20) = 3,840` pixels (7.68 KB each) allocated in SRAM (`LVGL_BUF_LEN`). Total buffer footprint is ~15.36 KB.
- **Tick Source:** Hardware `esp_timer` triggering `example_increase_lvgl_tick()` every 2ms.

### LVGL Configuration (`lv_conf.h`)
- **Version:** LVGL v8.3.9.
- **Memory Pool (`LV_MEM_SIZE`):** Increased to **128 KB** internal pool (57% dynamic SRAM used, 139 KB headroom remaining).
- **Fonts Available:** `LV_FONT_MONTSERRAT_12`, `14`, `16`, `20`, `24`, `32`.
- **Display Full Refresh:** `disp_drv.full_refresh = 1` is currently active.

---

## 4. Touch Controller & Coordinate Architecture

- **Controller:** CST328 5-point capacitive touch on `Wire1` (`0x1A`).
- **Interrupt:** `Touch_CST328_ISR` flags `Touch_interrupts = true`.
- **LVGL Ingest:** `Lvgl_Touchpad_Read()` reads coordinates into `touchpad_x[0]`, `touchpad_y[0]`.
- **Coordinate Mapping:** Native portrait coordinates `[0..239, 0..319]`. No rotation/inversion transposition required.

---

## 5. BLE, BMS Protocol, and Telemetry Data Flow

### Connection & GATT Quirks (Protected Subsystem)
1. **Active Scanning:** BMS advertises name `CE024AA005260138` in Scan Response. `setActiveScan(true)` is mandatory.
2. **Ghost Permissions:** Characteristic `0xff02` (RX) advertises as `[Read]` only in GATT table. Writes must be forced without response: `commandChar->writeValue(req, len, false)`.
3. **Core Version Compatibility:** `NimBLE-Arduino` master branch must be used to prevent ESP32 Core v3.x panic crashes.

### Protocol Framing & Decoding
- **Framing:** Start byte `0xDD`, End byte `0x77`. Length at byte index 3.
- **Basic Info (`0x03`):** Polled at 4Hz alternating. Decodes Pack Voltage (V), Current (A, signed int16), Remaining Ah, Design Ah, Lifetime Cycles, NTC Probe Temperature (°C), SOC (%), Alarm Bitmask, Charge/Discharge FET flags.
- **Cell Voltages (`0x04`):** Polled at 4Hz alternating. Decodes 2 bytes per cell (mV) into `cellVoltages[16]` and dynamic `cellCount`.

---

## 6. Concurrency and Thread Safety

- **Core Separation:** `logicTask` (Core 0) handles all network/BLE I/O. `uiTask` (Core 1) handles all UI rendering and touch processing.
- **Data Protection:** `telemetryMutex` guards the global `Telemetry` struct. `snapshotTelemetry()` performs an atomic copy of all telemetry metrics before passing to UI routines.
- **LVGL Thread Safety:** All LVGL API calls (`lv_label_set_text`, `lv_arc_set_angles`, `lv_bar_set_value`) are strictly isolated inside `uiTask` on Core 1. No LVGL calls are ever made from BLE ISRs or NimBLE callbacks.

---

## 7. Issue Classification

### Confirmed Source-Level Issues
1. **Transparent Navigation Button Hack:** `v3.ino` uses two large transparent buttons (`btn_next`, `btn_prev`) spanning `100px x 320px` on the left and right edges. This occupies 83% of the touch surface, preventing interactive widgets from receiving touch events.
2. **Truncated Cell Voltage Rendering:** `cells_scr` hardcodes 8 bars (`cell_bars[8]`), but the JBD BMS and `Telemetry` struct support up to 16 cells.
3. **`full_refresh = 1` with 1/20 Buffer:** Redrawing the entire display on every 5ms tick when only labels change causes unnecessary SPI bus traffic.

### Suspected Issues Requiring Testing
1. **Touch Release Invalidation:** `Touch_Get_XY()` zeroes `touch_data.points = 0` upon read. We must test whether dragging/swiping gestures require retaining point state across consecutive frames.
2. **Backlight Thermal Dissipation:** Backlight is currently set to 50% PWM. We should verify screen brightness under outdoor lighting vs thermal temperature on the ESP32-S3.

### Design Improvements Ready for Implementation
1. Large, crisp typography using newly unlocked Montserrat 20, 24, and 32.
2. Dynamic 3-Screen architecture:
   - **Screen 1 (Main Dashboard):** SOC Ring Gauge, Pack Volts, Current, Power (W), Status Pill.
   - **Screen 2 (Cell Diagnostics):** 16-Cell voltage grid/list with Min/Max/Delta highlight.
   - **Screen 3 (System Health & Alarms):** Cycles, Temperature, Remaining Ah / Capacity, Protection status flags.
3. True gesture navigation (`LV_EVENT_GESTURE`) or carousel swipe.

---

## 8. Protected Subsystems (DO NOT MODIFY)

The following components are verified working and protected from arbitrary refactoring:
- `parseJbdFrame()` and BLE packet buffering logic.
- `findAndConnectBattery()` and `pollBattery()`.
- `logicTask` execution loop and `telemetryMutex`.
- `Display_ST7789.cpp` SPI transaction calls.
- `pinMode(7, OUTPUT); digitalWrite(7, HIGH);` system power latch.

---

## 9. Next Steps
Phase 1 (Repository & Firmware Audit) is complete. The repository is in a clean, verified state. We are ready for **AGENT C (UI/UX and Motion Design Architect)** and **AGENT C2 (Graphics and Asset Specialist)** to produce the comprehensive design specification.
