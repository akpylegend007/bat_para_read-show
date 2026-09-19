# Conversation History & Project Evolution

This document tracks the step-by-step evolution of the project across AI chat sessions. It is maintained so future developers (and AI agents) understand *why* certain decisions were made and how the codebase evolved.

## Session 1: Initial BLE Fix & OLED Dashboard (ESP32 WROOM)
* **The Problem:** The user's ESP32 was connecting to the Clean Electric LFPGo400 BMS over BLE, but the 128x32 OLED display was blank/stuck on "Scanning" and no data was updating.
* **Fix 1 (Core Crashing):** Discovered the standard `NimBLE-Arduino` release (v1.4.x) caused `ESP_ERR_INVALID_STATE` crash loops on the newer ESP32 Core v3.x. Upgraded the local `lib/NimBLE-Arduino` to the `master` branch.
* **Fix 2 (Active Scanning):** Discovered the BMS broadcasts its unique name (`CE024AA005260138`) in the Scan Response packet. Enabled active scanning (`pBLEScan->setActiveScan(true)`).
* **Fix 3 (GATT Permissions Bug):** Found that the BMS's write characteristic (`0xff02`) deceptively advertised as `[Read]` only. Bypassed the standard `pChar->canWrite()` safety checks to force a write-without-response.
* **Result:** Successfully pulled the JBD 0x03 payload and implemented a 3-page cyclic dashboard (Main, System Health, Diagnostics).

## Session 2: The V2 Pacman Upgrade (TFT Display)
* **The Request:** The user dropped a new folder (`pacman_battery_ui_esp32`) containing a 128x128 ST7735 TFT demo script with a retro Pacman theme, and requested it be wired to the live BLE telemetry, with much larger fonts and red accents for discharging.
* **Implementation:** Created `v2/v2.ino`. Replaced the demo loop with the live NimBLE GATT extraction logic. 
* **Bug Fix:** The large layout left a tiny 12-pixel gap on the screen where the "SCANNING..." connection text was drawn. When live data streamed in, this text wasn't cleared, leaving a permanent graphical artifact. Fixed by expanding the `fillRect` bounding box in the `drawPelletBar` function to completely wipe the screen gap.

## Session 3: Hardware Migration Strategy (ESP32-C3)
* **The Request:** The user asked if migrating from the bulky ESP32-WROOM-32 to a tiny ESP32-C3 SuperMini would work.
* **Resolution:** Confirmed it is a highly feasible upgrade. The C3's RISC-V core and BT 5.0 LE radio are perfectly suited for this. 
* **Planning:** Mapped out that the V2 SPI display requires 5 pins (using C3 defaults SCK=4, MOSI=6), leaving ~8 free GPIOs on the SuperMini for future features (buzzers, buttons, I2C sensors, relay load disconnects). Documented in `DEVELOPER_HANDOFF.md`.
* **System Integration:** Established `GEMINI.md` Workspace Rules to force future AI agents to automatically read the Handoff and History documents before assisting the user.
