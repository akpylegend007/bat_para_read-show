# 🔋 LFPGo400 BLE Battery Dashboard

> A standalone ESP32 hardware monitor that bypasses the manufacturer's locked mobile app and pulls real-time telemetry directly from a Clean Electric LFPGo400 LiFePO₄ inverter battery over Bluetooth Low Energy.

This repository contains **three different hardware implementations** ranging from a simple OLED readout to a high-end, DMA-accelerated LVGL touch dashboard.

---

## 🛠️ The Hardware Targets

This project has evolved through three distinct UI versions, all powered by the ESP32 and NimBLE to read the raw Jiabaida (JBD) BMS protocol over GATT.

### V1: The Minimalist (0.91" OLED)
* **Microcontroller:** ESP32-WROOM-32 (NodeMCU)
* **Display:** 0.91" SSD1306 OLED (128x32 I2C)
* **UI:** A simple text-based interface that automatically cycles through three industrial-style pages (Main, System Health, Diagnostics) every 3.5 seconds.
* **Location:** `battry_indicator.ino`

### V2: The Pacman (1.44" TFT)
* **Microcontroller:** ESP32-C3 SuperMini (RISC-V)
* **Display:** 1.44" ST7735 SPI TFT (128x128)
* **UI:** A custom-drawn, retro "Pacman" themed dashboard utilizing `Adafruit_GFX`. The SOC percentage dictates how many "pellets" are left on the screen, while color-coded ghosts warn about BMS protection alarms (overvoltage, overtemp, etc).
* **Location:** `esp32_c3_pacman_ui/`

### V3: The Modern Dashboard (2.8" Touch TFT)
* **Microcontroller:** ESP32-S3 (8MB PSRAM, Dual-Core)
* **Display:** 2.8" ST7789 SPI Touch TFT (240x320)
* **UI:** A massive architectural upgrade powered by **LVGL 8.3**. Uses the official Waveshare hardware-accelerated DMA SPI driver for buttery smooth 60 FPS rendering. Features a glowing animated Pacman Arc, invisible touch-zones for swiping, and a dedicated **Cell Diagnostics** screen with individual progress bars for up to 16 individual cell voltages.
* **Polling:** Alternates `0x03` (Basic Info) and `0x04` (Cell Voltages) payloads at a rapid 4 FPS.
* **Location:** `v3/`

---

## 🚀 How It Works

```text
┌───────────────┐     BLE GATT      ┌───────────────┐     SPI / I2C     ┌───────────────┐
│   LFPGo400    │ ◀───────────────▶ │    ESP32      │ ───────────────▶  │    Display    │
│   Battery     │   JBD 0x03/0x04   │  (BLE Client) │                   │  OLED or TFT  │
│   (JBD BMS)   │                   │               │                   │               │
└───────────────┘   Notify (ff01)   └───────────────┘                   └───────────────┘
```

1. **Scan** — The ESP32 actively scans for the battery's BLE advertisement by name.
2. **Connect** — Locks onto the BMS GATT service (`0xff00`).
3. **Subscribe** — Registers for notifications on characteristic `0xff01` (TX).
4. **Poll** — Writes the JBD basic-info request (`0xDD 0xA5 0x03...`) to characteristic `0xff02` (RX). (V3 also alternatingly requests cell voltages via `0x04`).
5. **Parse** — Assembles fragmented BLE MTU packets into complete JBD frames and extracts voltage, current, SOC, temperature, capacity, cycles, cell voltages, and protection status.
6. **Render** — Pushes the parsed telemetry out to the displays.

---

## ⚡ Quick Start

1. **Clone this repo**
   ```bash
   git clone https://github.com/akpylegend007/bat_para_read-show.git
   ```

2. **Update the battery name:**
   Find `BATTERY_NAME_MATCH = "CE024AA005260138"` in the sketch for your desired version and replace it with your battery's BLE broadcast name.

3. **Compile & upload:**
   * **For V1 (ESP32 WROOM):**
     `arduino-cli compile -b esp32:esp32:esp32 --library lib/NimBLE-Arduino battry_indicator.ino`
   * **For V2 (ESP32-C3):**
     `arduino-cli compile -b esp32:esp32:esp32c3 --library lib/NimBLE-Arduino esp32_c3_pacman_ui/esp32_c3_pacman_ui.ino`
   * **For V3 (ESP32-S3 LVGL):**
     `arduino-cli compile -b esp32:esp32:esp32s3 --board-options "PartitionScheme=huge_app" --library "esp32 2.8_offical_example_library/Arduino/libraries/lvgl" --library lib/NimBLE-Arduino v3/v3.ino`
     *(Note: V3 requires the `Huge APP` or `8MB` partition scheme due to LVGL's size!)*

4. **Disconnect all other BLE clients** (phone apps, Overkill Solar, etc.) — standard BLE only allows one connection at a time.

---

## 🧩 Dependencies

| Library | Version | Notes |
|---------|---------|-------|
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | `master` branch | **Required** for ESP32 Arduino Core v3.x compatibility |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) | Latest | OLED driver (V1) |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | Latest | Graphics primitives (V1, V2) |
| [LVGL](https://lvgl.io/) | v8.3.x | Retained-mode GUI framework (V3) |

> **⚠️ Important:** If you are using ESP32 Arduino Core v3.x (3.3.10+), the standard NimBLE-Arduino v1.4.x release will cause boot crashes (`ESP_ERR_INVALID_STATE`). You **must** use the `master` branch.

---

## 📱 Compatibility

This firmware should work out-of-the-box with **any battery** that uses a **JBD (Jiabaida) Smart BMS**, including but not limited to:
- Clean Electric LFPGo400
- Overkill Solar BMS
- DALY Smart BMS (JBD variant)
- Generic JBD BMS boards (SP15S020, SP21S020, etc.)

The JBD protocol is the same across all these brands — only the BLE advertisement name differs.

---

## 📝 License

MIT — do whatever you want with it. If you build one, we'd love to see it!
