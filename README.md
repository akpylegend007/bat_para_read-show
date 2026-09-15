# ⚡ LFPGo400 BLE Battery Dashboard

> A standalone ESP32 hardware monitor that bypasses the manufacturer's locked mobile app and pulls real-time telemetry directly from a Clean Electric LFPGo400 LiFePO₄ inverter battery over Bluetooth Low Energy.

---

## 🔋 The Problem

Commercial LFP batteries like the **Clean Electric LFPGo400** (25.6V, 100Ah) ship with a proprietary mobile app that locks down direct monitoring flexibility. Configuration parameters are restricted, data export is nonexistent, and you're tethered to your phone just to check your battery's charge level.

## 💡 The Solution

This project turns a **$5 ESP32** and a **$3 OLED display** into a dedicated, always-on battery dashboard. The ESP32 acts as a passive BLE client — it connects directly to the battery's internal Smart BMS, speaks the raw JBD (Jiabaida) protocol over GATT, and renders live telemetry onto a compact 0.91" dual-color OLED screen.

**No phone. No app. No cloud. Just your battery's raw truth on a tiny screen.**

---

## 📟 Display Pages

The dashboard automatically cycles through **3 industrial-style pages** every 3.5 seconds:

### Page 1 — Main Dashboard
```
┌────────────────────────┐
│ CLEAN ELEC    SOC:87%  │  ← Yellow zone (header)
│────────────────────────│
│   26.4V  +12A          │  ← Blue zone (large text)
└────────────────────────┘
```
Pack voltage and live current draw at a glance. Positive current = charging, negative = discharging.

### Page 2 — System Health
```
┌────────────────────────┐
│ --- SYSTEM HEALTH ---  │
│ Cap: 95.2/100.0Ah      │
│ Cyc: 142    T: 31C     │
└────────────────────────┘
```
Remaining capacity vs design capacity, total lifetime charge cycles, and peak cell temperature.

### Page 3 — Diagnostics
```
┌────────────────────────┐
│ --- DIAGNOSTICS ---    │
│ Mode: Charging         │
│ Alarms: NONE (OK)      │
└────────────────────────┘
```
Current operating mode and BMS protection/alarm status codes (overvoltage, undervoltage, overcurrent, short circuit, overtemp).

---

## 🛠 Hardware Required

| Component | Specification |
|-----------|--------------|
| **Microcontroller** | ESP32-WROOM-32 (any dev board) |
| **Display** | 0.91" SSD1306 OLED, 128×32, I²C |
| **Battery** | Clean Electric LFPGo400 (or any JBD/Overkill Solar compatible BMS) |

### Wiring

```
ESP32          OLED (SSD1306)
─────          ──────────────
GPIO 21  ───►  SDA
GPIO 22  ───►  SCL
3.3V     ───►  VCC
GND      ───►  GND
```

**Total cost: ~$8**

---

## 📡 How It Works

```
┌──────────────┐     BLE GATT      ┌──────────────┐     I²C      ┌──────────────┐
│   LFPGo400   │ ◄──────────────── │    ESP32      │ ──────────► │  SSD1306     │
│   Battery    │   JBD 0x03 Proto  │  (BLE Client) │             │  OLED        │
│   (BMS)      │ ──────────────► │              │             │  128×32      │
└──────────────┘   Notify (ff01)   └──────────────┘             └──────────────┘
```

1. **Scan** — ESP32 actively scans for the battery's BLE advertisement by name.
2. **Connect** — Locks onto the BMS GATT service (`0xff00`).
3. **Subscribe** — Registers for notifications on characteristic `0xff01` (TX).
4. **Poll** — Every 2 seconds, writes the JBD basic-info request (`0xDD 0xA5 0x03...`) to characteristic `0xff02` (RX).
5. **Parse** — Assembles fragmented BLE packets into complete JBD frames, extracts voltage, current, SOC, temperature, capacity, cycles, and protection status.
6. **Render** — Updates the OLED with the parsed telemetry across 3 rotating pages.

---

## 📦 Dependencies

| Library | Version | Notes |
|---------|---------|-------|
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | `master` branch | **Required** for ESP32 Arduino Core v3.x compatibility |
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) | Latest | OLED driver |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | Latest | Graphics primitives |

> **⚠️ Important:** If you are using ESP32 Arduino Core v3.x (3.3.10+), the standard NimBLE-Arduino v1.4.x release will cause boot crashes (`ESP_ERR_INVALID_STATE`). You **must** use the `master` branch.

---

## 🚀 Quick Start

1. **Clone this repo**
   ```bash
   git clone https://github.com/YOUR_USERNAME/LFPGo400-Dashboard.git
   ```

2. **Update the battery name** in `battry_indicator.ino` (line 264):
   ```cpp
   // Replace CE024AA005260138 with your battery's BLE broadcast name
   advertisedDevice->getName().find("CE024AA005260138")
   ```

3. **Compile & upload** using Arduino CLI:
   ```bash
   arduino-cli compile -b esp32:esp32:esp32 --library lib/NimBLE-Arduino --library lib/Adafruit_SSD1306 --library lib/Adafruit-GFX-Library -p COM12 --upload battry_indicator.ino
   ```

4. **Disconnect all other BLE clients** (phone apps, Overkill Solar, etc.) — standard BLE only allows one connection at a time.

5. Watch the dashboard light up! 🎉

---

## 🔧 Compatibility

This firmware should work with **any battery** that uses a **JBD (Jiabaida) Smart BMS**, including but not limited to:

- Clean Electric LFPGo400
- Overkill Solar BMS
- DALY Smart BMS (JBD variant)
- Generic JBD BMS boards (SP15S020, SP21S020, etc.)

The JBD protocol is the same across all these brands — only the BLE advertisement name differs.

---

## 📝 Telemetry Fields

| Field | Source | Update Rate |
|-------|--------|-------------|
| Pack Voltage | JBD 0x03 bytes 4-5 | 2s |
| Pack Current | JBD 0x03 bytes 6-7 (signed) | 2s |
| State of Charge (%) | JBD 0x03 byte 23 | 2s |
| Remaining Capacity (Ah) | JBD 0x03 bytes 8-9 | 2s |
| Design Capacity (Ah) | JBD 0x03 bytes 10-11 | 2s |
| Cycle Count | JBD 0x03 bytes 12-13 | 2s |
| Protection Status | JBD 0x03 bytes 20-21 | 2s |
| Cell Temperature (°C) | JBD 0x03 NTC probes | 2s |

---

## 📄 License

MIT — do whatever you want with it. If you build one, I'd love to see it!
