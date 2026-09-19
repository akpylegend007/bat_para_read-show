# Developer Handoff & Technical Context

This document contains critical technical context, debugging history, and protocol reverse-engineering notes accumulated during the development of this project. It is intended for any human developer (or AI assistant) taking over the codebase to prevent relearning the same hard lessons.

## 1. Hardware & Environment
* **Target:** ESP32 (WROOM-32, ESP32-C3 SuperMini, Waveshare ESP32-S3 Touch LCD 2.8)
* **Battery:** Clean Electric LFPGo400 (25.6V, 100Ah LiFePO4)
* **BMS Hardware:** Jiabaida (JBD) / Overkill Solar Smart BMS
* **BLE Broadcast Name:** `CE024AA005260138` (Unique to this pack)
* **Framework:** Arduino core for ESP32 (v3.x)

## 2. Critical BLE & GATT Quirks (DO NOT MODIFY)
The JBD BMS has several major BLE implementation quirks that required specific workarounds:

1. **Active Scanning is Mandatory:** The BMS broadcasts its name (`CE024AA005260138`) in the **Scan Response** packet, not the primary advertisement. If you set `pBLEScan->setActiveScan(false)`, the ESP32 will see the MAC address but the name will be empty, and connection will fail.
2. **Ghost `[Write]` Permission:** The BMS uses Service `0xff00`. 
   * `0xff01` is the TX characteristic (Notify).
   * `0xff02` is the RX characteristic (Write).
   * **BUG:** The BMS GATT table advertises `0xff02` as `[Read]` only! Standard library checks like `pRxChar->canWrite()` will return `false`. You **must bypass safety checks** and force a write-without-response to `0xff02`, otherwise the BMS will never send data.
3. **NimBLE Library Version:** Standard release versions of `NimBLE-Arduino` (e.g., v1.4.2) crash the ESP32 in an infinite reboot loop (`ESP_ERR_INVALID_STATE`) when compiled against ESP32 Core v3.x. You **must** use the `master` branch of NimBLE-Arduino.
4. **Single Connection Limit:** The BMS only allows one BLE client at a time. If the official Overkill Solar or Clean Electric mobile app is running in the background on a phone, the ESP32 will fail to connect.

## 3. The JBD Protocols (0x03 Basic Info & 0x04 Cell Voltages)
To request telemetry, the ESP32 alternates requests between Basic Info and Cell Voltages at 4Hz (250ms):
* **0x03 Basic Info Request:** `{0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77}`
* **0x04 Cell Voltages Request:** `{0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77}`

The BMS replies via notifications on `0xff01`. The reply is fragmented across multiple BLE MTU packets.
* Packets must be buffered in `rxFrame`. 
* Start byte is `0xDD`, End byte is `0x77`.
* Byte 3 is the length of the data payload. Total expected packet length is `4 (Header) + Length + 2 (Checksum) + 1 (Footer)`.

**Extracted Byte Offsets (0x03):**
* `4,5`: Pack Voltage (divide by 100)
* `6,7`: Pack Current (signed int16, divide by 100). Positive = Charge, Negative = Discharge.
* `8,9`: Remaining Capacity (Ah, x10/1000)
* `10,11`: Design Capacity (Ah, x10/1000)
* `12,13`: Cycle Count
* `16,17`: Protection/Alarm Status (Bitmask)
* `23`: State of Charge (SOC %)
* `24`: FET Status (Bit 0: Charge FET, Bit 1: Discharge FET)
* `25`: Number of Cells
* `27,28`: Temperature data (Raw Kelvin * 10). Formula: `(Raw - 2731) / 10.0 = Celsius`.

**Extracted Byte Offsets (0x04):**
* `4+ (i*2), 5+ (i*2)`: Millivolts per individual cell for `i` in `0..cellCount-1`.

## 4. Waveshare ESP32-S3 Touch LCD 2.8 Hardware (SKU: 27690)
* **SoC:** ESP32-S3R8 (240MHz dual-core LX7, 8MB PSRAM, 16MB Flash)
* **Display:** ST7789 IPS 240x320 Portrait via FSPI (MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=5 at 80MHz)
* **Touch:** CST328 5-point Capacitive Touch on `Wire1` (SDA=1, SCL=3, INT=4, RST=2, Address=0x1A)
* **Power Latch:** GPIO 7 must be asserted `HIGH` at boot to latch board power. GPIO 6 is PWR_KEY.
* **Backlight:** LEDC PWM 20kHz, set to 50% duty cycle (`Set_Backlight(50)`) for thermal management.

## 5. LVGL 8.3 Architecture & UI System (V3)
* **Engine:** LVGL 8.3.9 retained-mode UI framework.
* **Memory:** 128 KB internal memory pool (`LV_MEM_SIZE`) in `lv_conf.h`.
* **Refresh Mode:** Partial dirty-rectangle rendering enabled (`disp_drv.full_refresh = 0`).
* **Concurrency:**
  * `logicTask` (Core 0, Priority 1, 8KB): BLE connection, framing, alternating 4Hz polling.
  * `uiTask` (Core 1, Priority 1, 16KB): Drives `Lvgl_Loop()` and atomic UI updates via `snapshotTelemetry()`.
  * `telemetryMutex`: Protects telemetry reads/writes across cores.
* **Screens:**
  1. **Screen 0 (Splash / Boot):** Animated battery icon, loading progress bar, pulsing scan status. Auto-transitions to Dashboard on connect.
  2. **Screen 1 (Main Dashboard):** 160x160 SOC Ring Gauge (270° sweep, dynamic Green/Gold/Red indicator, 32px center text), Status Bar with connection & temp, Dynamic Status Pill (+CHARGING, -DISCHARGING, -IDLE, X DISCONNECTED), Dual Cards for Voltage (20px Cyan) and Current (20px Dynamic).
  3. **Screen 2 (Cell Diagnostics):** 4x4 Grid supporting up to 16 cells dynamically with per-cell mV readouts, mini horizontal health bars, min/max/critical cell color highlights, and bottom MIN/MAX/DELTA summary card.
  4. **Screen 3 (System Health & Alarms):** Total/Remaining Capacity card with progress bar, Cycle count, Pack Temperature, 5-Alarm Protection Grid (OV, UV, OC, SC, OT), and FET Relay Status (CHG/DCHG ON/OFF).
* **Navigation:** Native LVGL gesture handler (`LV_EVENT_GESTURE`) with left/right swipe animations (`LV_SCR_LOAD_ANIM_MOVE_LEFT/RIGHT`) and bottom 3-dot page indicator with touch tap fallback.

## 6. Performance & Animation Review (Agent E)
- A full review was performed to ensure V3 graphics and animations safely coexist with the BLE NimBLE stack.
- Splash screen animations (`lv_anim_t` on loading bar and status label opacity) were verified to be non-blocking.
- NimBLE `scan->start(0, false)` was verified to be non-blocking in NimBLE 3.0 API, protecting `logicTask` from hanging.
- Total memory (128KB LVGL heap) and partial rendering bounds are strictly preserved and within safe Flash/RAM margins (33%/57%).

## 7. Visual & Hardware QA Signoff (Agent F)
- **Status: PASS.** The UI matches the design specifications.
- Layouts fit the 240x320 constraint perfectly, colors correctly represent telemetry states (Green/Gold/Red/Cyan), and LVGL components are efficiently reused to prevent memory fragmentation.
- Primary gesture swipe navigation is robust. Note: The tap-targets for the pagination dots (8x8px) are quite small physically and may need extended click areas in future polish passes.
- Ready for physical hardware flashing and end-user testing.