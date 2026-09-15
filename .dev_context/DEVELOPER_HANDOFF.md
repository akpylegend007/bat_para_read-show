# Developer Handoff & Technical Context

This document contains critical technical context, debugging history, and protocol reverse-engineering notes accumulated during the development of this project. It is intended for any human developer (or AI assistant) taking over the codebase to prevent relearning the same hard lessons.

## 1. Hardware & Environment
* **Target:** ESP32 (WROOM-32 or similar)
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

## 3. The JBD 0x03 Protocol Payload
To request telemetry, the ESP32 writes the Basic Info Request frame to `0xff02` every 2 seconds:
`{0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77}`

The BMS replies via notifications on `0xff01`. The reply is fragmented across multiple BLE MTU packets.
* Packets must be buffered in `packetBuffer`. 
* Start byte is `0xDD`, End byte is `0x77`.
* Byte 3 is the length of the data payload. Total expected packet length is `4 (Header) + Length + 2 (Checksum) + 1 (Footer)`.

**Extracted Byte Offsets:**
* `4,5`: Pack Voltage (divide by 100)
* `6,7`: Pack Current (signed int16, divide by 100). Positive = Charge, Negative = Discharge.
* `8,9`: Remaining Capacity (Ah, x10/1000)
* `10,11`: Design Capacity (Ah, x10/1000)
* `12,13`: Cycle Count
* `16,17`: Protection/Alarm Status (Bitmask)
* `19`: State of Charge (SOC %)
* `22`: Number of NTC Temperature probes
* `23+`: Temperature data (Raw Kelvin * 10). Formula: `(Raw - 2731) / 10.0 = Celsius`.

## 4. UI Versions
### v1 (OLED 128x32)
* Uses `Adafruit_SSD1306`. I2C pins: SDA=21, SCL=22.
* Rotates through 3 pages (Main, Health, Diagnostics) every 3.5 seconds.
* Source: `battry_indicator.ino`

### v2 (TFT 128x128)
* Uses `Adafruit_ST7735`. SPI pins: CS=5, DC=2, RST=4. SCL=18, SDA=23 (Hardware VSPI).
* Single-page "Pacman" themed UI.
* Fixed an overlapping artifact bug where the "SCANNING..." text gap wasn't cleared during the live telemetry render loop. Oversized fonts (Size 4 for SOC, Size 2 for text). Red layout for discharging.
* Source: `v2/v2.ino`

## 5. Deployment / Flashing Notes
* Auto-reset via DTR/RTS is unreliable on the user's dev board. 
* The **BOOT button must be held manually** when `esptool.py` prints `Connecting...` to successfully flash.
* `arduino-cli` was used for all compilation and flashing. Ensure the serial monitor is closed before flashing to prevent `Access Denied` COM port errors.
## 6. Hardware Migration: ESP32-C3 SuperMini
The project is being migrated from the dual-core ESP32-WROOM-32 to the single-core RISC-V **ESP32-C3 SuperMini**.
* **Feasibility:** Fully supported. The C3's Bluetooth 5.0 LE radio handles the JBD BMS scanning perfectly, and NimBLE-Arduino + Adafruit_GFX are fully compatible.
* **Pin Configuration:** The ESP32-C3 SuperMini breaks out 13 usable GPIOs (0-10, 20, 21). 
* **V2 TFT Wiring on C3:** The ST7735 SPI display requires 5 pins. Default hardware SPI on C3 is SCK=4, MOSI=6. CS, DC, and RST can be mapped to any available GPIOs.
* **Expansion Potential:** Wiring the display leaves ~8 free GPIOs on the SuperMini, which can be used for future features: physical buttons/encoders for UI navigation, buzzers for BMS alarms, Neopixel status LEDs, relay load control, or I2C environmental sensors (BME280).
* **Compilation:** Requires changing the Arduino CLI board target from esp32:esp32:esp32 to esp32:esp32:esp32c3.
