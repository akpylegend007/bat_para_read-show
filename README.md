# Clean Electric LFPGo400 BMS Dashboard

This project creates a dedicated ESP32-based Bluetooth Low Energy (BLE) dashboard for the Clean Electric LFPGo400 (or any compatible Jiabaida/Overkill Solar BMS). It passively connects to the BMS over BLE and extracts the live telemetry using the JBD 0x03 protocol payload.

## Features
* **Completely Wireless**: ESP32 runs standalone and pulls data directly via Bluetooth.
* **Industrial Multipage UI**: 128x32 OLED automatically cycles through:
  * **Main Dash:** Live SOC %, Voltage, and Current.
  * **System Health:** Actual Capacity vs Design Capacity, Cycle count, and Maximum Temperature.
  * **Diagnostics:** Operating Mode (Charge/Discharge/Idle) and internal Alarm codes.

## Hardware Required
* ESP32 Development Board (e.g. ESP32-WROOM)
* 0.91-inch SSD1306 I2C OLED display (128x32)

### Wiring
* OLED **SDA** -> ESP32 **GPIO 21**
* OLED **SCL** -> ESP32 **GPIO 22**
* OLED **VCC** -> ESP32 **3.3V**
* OLED **GND** -> ESP32 **GND**

## Dependencies
You will need the following libraries installed in your Arduino IDE or PlatformIO:
* `NimBLE-Arduino` (Note: Must use the `master` branch if using ESP32 Core v3.x)
* `Adafruit SSD1306`
* `Adafruit GFX Library`

## Setup & Compilation
1. Update `battry_indicator.ino` with the exact BLE name of your battery (default is `CE024AA005260138`).
2. Compile and upload to your ESP32.
3. Ensure you completely disconnect the official BMS phone app, as standard BLE limits the BMS to one connection at a time.
