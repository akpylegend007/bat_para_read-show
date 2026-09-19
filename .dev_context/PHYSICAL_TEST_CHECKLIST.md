# PHYSICAL HARDWARE VALIDATION CHECKLIST

**Device:** Waveshare ESP32-S3 Touch LCD 2.8-inch
**Firmware Version:** V3 Dashboard

## Pre-Requisites
- [ ] ESP32-S3 successfully flashed with latest compiled firmware (`arduino-cli` or IDE).
- [ ] Waveshare LCD board powered on.
- [ ] JBD BMS battery active and within BLE range (~5 meters).

## 1. Boot & Connection
- [ ] **Splash Screen Animation:** Does the loading bar sweep smoothly? Does the "SCANNING FOR BMS..." text pulse without stuttering?
- [ ] **BLE Discovery:** Does the device successfully transition out of the splash screen once connected to the `CE024AA005260138` BMS?
- [ ] **Connection Recovery:** Turn off or move the battery out of range. Does the UI drop to `--` values and pulse red? Move battery back in range. Does it recover without rebooting?

## 2. Display Rendering
- [ ] **TFT Tearing:** Observe rapid SOC updates. Is there any diagonal tearing across the screen? (Check `LVGL_Driver.cpp` partial refresh stability).
- [ ] **Color Accuracy:** Do the `ACCENT_CYAN` (Cyan) and `ACCENT_GOLD` (Gold) values render accurately without color wash-out?
- [ ] **Image Retention:** Leave the dashboard on Screen 1 for 1 hour. Are there any permanent ghosting artifacts on static labels (e.g., "LFPGo400")?

## 3. Touch & Interaction
- [ ] **Swipe Gesture:** Swipe left and right across the center of the screen. Does the screen transition trigger consistently?
- [ ] **Pagination Dots (Tap):** Attempt to precisely tap the bottom 3 navigation dots. Does the extended 15px hit area register the touch accurately?
- [ ] **False Triggers:** Does swiping accidentally trigger any other UI elements?

## 4. Telemetry Latency
- [ ] **Update Rate:** Does the voltage/current data appear to update at ~4Hz (approx 250ms) as designed in `logicTask`?
- [ ] **Animation Smoothness:** When current swings wildly, do the cell bars (`LV_ANIM_ON`) glide to their new values smoothly rather than instantly snapping?

## 5. Stability
- [ ] **Long-term Watchdog Test:** Run the device overnight (12+ hours). Does it remain responsive to touch? Are there any unexpected reboots due to `logicTask` or `uiTask` watchdog starvation?
