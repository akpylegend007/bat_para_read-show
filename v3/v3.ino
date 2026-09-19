#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <NimBLEDevice.h>
#include <lvgl.h>

#include "Display_ST7789.h"
#include "Touch_CST328.h"
#include "LVGL_Driver.h"

// --- BLE & BMS State ---
static const char *BATTERY_NAME_MATCH = "CE024AA005260138";

struct Telemetry {
  uint32_t timestampMs = 0;
  float voltage = 0, current = 0, remainingAh = 0, designAh = 0;
  uint16_t cycles = 0;
  int8_t temperatureC = 0;
  uint16_t protection = 0;
  uint32_t balanceMaskLow = 0, balanceMaskHigh = 0;
  uint8_t soc = 0;
  bool chargeFet = false, dischargeFet = false;
  int8_t rssi = 0;
  // Cell voltages
  uint16_t cellVoltages[16] = {0};
  uint8_t cellCount = 0;
};

static Telemetry telemetry;
static SemaphoreHandle_t telemetryMutex;
static NimBLEClient *client = nullptr;
static NimBLERemoteCharacteristic *commandChar = nullptr;
static NimBLEAddress batteryAddress(uint64_t(0), 0);
static bool haveBatteryAddress = false, batteryConnected = false, scanRunning = false;
static uint8_t rxFrame[256];
static size_t rxLength = 0;

static Telemetry snapshotTelemetry() {
  Telemetry copy;
  xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  copy = telemetry;
  xSemaphoreGive(telemetryMutex);
  return copy;
}

// UI Elements
static lv_obj_t *main_scr;
static lv_obj_t *cells_scr;

static lv_obj_t *soc_label;
static lv_obj_t *voltage_label;
static lv_obj_t *current_label;
static lv_obj_t *pacman_arc;

static lv_obj_t *cell_bars[16];
static lv_obj_t *cell_labels[16];

static void draw_pacman_arc(lv_obj_t *parent) {
    pacman_arc = lv_arc_create(parent);
    lv_obj_set_size(pacman_arc, 150, 150);
    lv_arc_set_rotation(pacman_arc, 0);
    lv_arc_set_bg_angles(pacman_arc, 0, 360);
    lv_arc_set_value(pacman_arc, 100);
    
    // Make background arc black
    lv_obj_set_style_arc_color(pacman_arc, lv_color_hex(0x000000), LV_PART_MAIN);
    // Make foreground arc yellow
    lv_obj_set_style_arc_color(pacman_arc, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(pacman_arc, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(pacman_arc, 20, LV_PART_MAIN);
    lv_obj_remove_style(pacman_arc, NULL, LV_PART_KNOB); // Remove knob
    lv_obj_clear_flag(pacman_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(pacman_arc, LV_ALIGN_CENTER, 0, -40);
}

static void updateUI(const Telemetry &d) {
  if (soc_label) {
    lv_label_set_text_fmt(soc_label, "%u%%", d.soc);
    // Pacman mouth opens wider when SOC is low!
    int mouth_angle = 30 + (100 - d.soc); // At 100%, mouth is 30deg. At 0%, mouth is 130deg!
    if (mouth_angle > 90) mouth_angle = 90;
    lv_arc_set_angles(pacman_arc, mouth_angle, 360 - mouth_angle);
  }
  if (voltage_label) lv_label_set_text_fmt(voltage_label, "%.1f V", d.voltage);
  if (current_label) lv_label_set_text_fmt(current_label, "%+.1f A", d.current);
  
  // Update cells
  if (cells_scr && d.cellCount > 0) {
    for (int i=0; i<d.cellCount && i<16; i++) {
        if (cell_bars[i]) {
            lv_bar_set_value(cell_bars[i], d.cellVoltages[i], LV_ANIM_ON);
            lv_label_set_text_fmt(cell_labels[i], "%u", d.cellVoltages[i]);
        }
    }
  }
}

static void parseJbdFrame(const uint8_t *data, size_t length) {
  if (length < 7 || data[0] != 0xDD) return;
  
  if (data[1] == 0x03 && data[2] == 0x00 && length >= 28) { // Basic Info
    float volts = ((data[4] << 8) | data[5]) * 0.01f;
    float amps = (int16_t)((data[6] << 8) | data[7]) * 0.01f;
    float remAh = ((data[8] << 8) | data[9]) * 0.01f;
    float desAh = ((data[10] << 8) | data[11]) * 0.01f;
    uint16_t cyc = (data[12] << 8) | data[13];
    uint32_t bHigh = (data[16] << 8) | data[17], bLow = (data[18] << 8) | data[19];
    uint16_t prot = (data[20] << 8) | data[21];
    uint8_t s = data[23];
    bool cf = data[24] & 1, df = data[24] & 2;
    uint8_t count = data[25];
    int8_t tempC = (int8_t)(((data[27] << 8) | data[28]) - 2731) / 10;
    
    xSemaphoreTake(telemetryMutex, portMAX_DELAY);
    telemetry.timestampMs = millis();
    telemetry.voltage = volts; telemetry.current = amps;
    telemetry.remainingAh = remAh; telemetry.designAh = desAh;
    telemetry.cycles = cyc; telemetry.temperatureC = tempC;
    telemetry.protection = prot; telemetry.balanceMaskHigh = bHigh;
    telemetry.balanceMaskLow = bLow; telemetry.soc = s;
    telemetry.chargeFet = cf; telemetry.dischargeFet = df;
    telemetry.cellCount = count;
    xSemaphoreGive(telemetryMutex);
  } else if (data[1] == 0x04 && data[2] == 0x00 && length >= (data[3] + 7)) { // Cell Voltages
    uint8_t count = data[3] / 2;
    if (count > 16) count = 16;
    xSemaphoreTake(telemetryMutex, portMAX_DELAY);
    for (uint8_t i = 0; i < count; i++) {
      telemetry.cellVoltages[i] = (data[4 + (i * 2)] << 8) | data[5 + (i * 2)];
    }
    telemetry.cellCount = count;
    xSemaphoreGive(telemetryMutex);
  }
}

static void notificationCallback(NimBLERemoteCharacteristic *, uint8_t *payload, size_t length, bool) {
  for (size_t i = 0; i < length; ++i) {
    if (rxLength == 0 && payload[i] != 0xDD) continue;
    if (rxLength >= sizeof(rxFrame)) { rxLength = 0; continue; }
    rxFrame[rxLength++] = payload[i];
    if (rxLength >= 4) {
      size_t expected = static_cast<size_t>(rxFrame[3]) + 7;
      if (expected > sizeof(rxFrame)) rxLength = 0;
      else if (rxLength == expected) { parseJbdFrame(rxFrame, rxLength); rxLength = 0; }
    }
  }
}

class BatteryAdvertisedCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *device) override {
    if (!device->haveName()) return;
    std::string found = device->getName();
    if (found.find(BATTERY_NAME_MATCH) == std::string::npos) return;
    batteryAddress = device->getAddress(); haveBatteryAddress = true; scanRunning = false;
    Serial.printf("battery found: %s (%s)\n", found.c_str(), batteryAddress.toString().c_str());
    NimBLEDevice::getScan()->stop();
  }
};

class BatteryClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) override {
    batteryConnected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(NimBLEClient *, int reason) override {
    batteryConnected = false; commandChar = nullptr; haveBatteryAddress = false;
    Serial.printf("BLE disconnected, reason=%d\n", reason);
  }
};

static bool findAndConnectBattery() {
  if (!haveBatteryAddress) {
    if (!scanRunning) {
      NimBLEScan *scan = NimBLEDevice::getScan(); scan->setScanCallbacks(new BatteryAdvertisedCallbacks(), true);
      scan->setInterval(80); scan->setWindow(60); scan->setActiveScan(true); scanRunning = true;
      scan->start(0, false);
    }
    return false;
  }
  if (client) { NimBLEDevice::deleteClient(client); client = nullptr; }
  client = NimBLEDevice::createClient(); client->setClientCallbacks(new BatteryClientCallbacks(), true);
  if (!client->connect(batteryAddress)) { NimBLEDevice::deleteClient(client); client = nullptr; haveBatteryAddress = false; return false; }
  NimBLERemoteService *service = client->getService("0000ff00-0000-1000-8000-00805f9b34fb");
  if (!service) { client->disconnect(); return false; }
  NimBLERemoteCharacteristic *notifyChar = service->getCharacteristic("0000ff01-0000-1000-8000-00805f9b34fb");
  commandChar = service->getCharacteristic("0000ff02-0000-1000-8000-00805f9b34fb");
  if (!notifyChar || !commandChar || !notifyChar->subscribe(true, notificationCallback)) { client->disconnect(); commandChar = nullptr; return false; }
  return true;
}

static void pollBattery() {
  static const uint8_t req_basic[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
  static const uint8_t req_cells[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};
  static bool pollToggle = false;
  
  if (!client || !client->isConnected() || !commandChar) return;
  
  if (pollToggle) commandChar->writeValue(req_cells, sizeof(req_cells), false);
  else commandChar->writeValue(req_basic, sizeof(req_basic), false);
  
  pollToggle = !pollToggle;
}

static void logicTask(void *) {
  uint32_t lastConnectMs = 0, lastPollMs = 0;
  for (;;) {
    uint32_t now = millis();
    if ((!client || !client->isConnected()) && now - lastConnectMs >= 6000) { lastConnectMs = now; findAndConnectBattery(); }
    if (batteryConnected && now - lastPollMs >= 250) { lastPollMs = now; pollBattery(); }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        if (lv_scr_act() == main_scr) lv_scr_load_anim(cells_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        else lv_scr_load_anim(main_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}

static void uiTask(void *) {
  for (;;) {
    Lvgl_Loop();
    updateUI(snapshotTelemetry());
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  Serial.begin(115200);
  telemetryMutex = xSemaphoreCreateMutex();
  
  pinMode(6, INPUT); // PWR_KEY_INPUT
  pinMode(7, OUTPUT); // PWR_CONTROL
  digitalWrite(7, HIGH);
  
  // Initialize Hardware
  LCD_Init();
  Touch_Init();
  Backlight_Init();
  Set_Backlight(50); // 50% PWM
  
  Lvgl_Init();
  
  // Build Basic UI - Main Screen
  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, lv_color_hex(0x000000), 0);
  
  draw_pacman_arc(main_scr);
  
  soc_label = lv_label_create(main_scr);
  lv_obj_set_style_text_color(soc_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(soc_label, &lv_font_montserrat_14, 0);
  lv_obj_align(soc_label, LV_ALIGN_CENTER, 0, -40);
  
  voltage_label = lv_label_create(main_scr);
  lv_obj_set_style_text_color(voltage_label, lv_color_hex(0x00FFFF), 0);
  lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_14, 0);
  lv_obj_align(voltage_label, LV_ALIGN_CENTER, -60, 80);
  
  current_label = lv_label_create(main_scr);
  lv_obj_set_style_text_color(current_label, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(current_label, &lv_font_montserrat_14, 0);
  lv_obj_align(current_label, LV_ALIGN_CENTER, 60, 80);
  
  lv_obj_t *btn_next = lv_btn_create(main_scr);
  lv_obj_set_size(btn_next, 100, 320);
  lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_next, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(btn_next, btn_event_cb, LV_EVENT_ALL, NULL);

  // Build Cells UI - Cells Screen
  cells_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(cells_scr, lv_color_hex(0x000000), 0);
  
  lv_obj_t *cells_title = lv_label_create(cells_scr);
  lv_obj_set_style_text_color(cells_title, lv_color_hex(0xFFFF00), 0);
  lv_obj_set_style_text_font(cells_title, &lv_font_montserrat_14, 0);
  lv_label_set_text(cells_title, "CELL VOLTAGES (mV)");
  lv_obj_align(cells_title, LV_ALIGN_TOP_MID, 0, 10);
  
  for (int i=0; i<8; i++) { // Limit to 8 for simple layout
      cell_bars[i] = lv_bar_create(cells_scr);
      lv_obj_set_size(cell_bars[i], 120, 15);
      lv_bar_set_range(cell_bars[i], 2500, 3650); // LFP bounds
      lv_obj_align(cell_bars[i], LV_ALIGN_TOP_LEFT, 20, 40 + i*25);
      lv_obj_set_style_bg_color(cell_bars[i], lv_color_hex(0x333333), LV_PART_MAIN);
      lv_obj_set_style_bg_color(cell_bars[i], lv_color_hex(0x00FF00), LV_PART_INDICATOR);
      
      cell_labels[i] = lv_label_create(cells_scr);
      lv_obj_set_style_text_color(cell_labels[i], lv_color_hex(0xFFFFFF), 0);
      lv_label_set_text(cell_labels[i], "0");
      lv_obj_align_to(cell_labels[i], cell_bars[i], LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  }
  
  lv_obj_t *btn_prev = lv_btn_create(cells_scr);
  lv_obj_set_size(btn_prev, 100, 320);
  lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_prev, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(btn_prev, btn_event_cb, LV_EVENT_ALL, NULL);

  lv_scr_load(main_scr);

  NimBLEDevice::init("LFPGo400 Dashboard");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  xTaskCreatePinnedToCore(logicTask, "bms_logic", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(uiTask, "display_ui", 16384, nullptr, 1, nullptr, 1);
}

void loop() { vTaskDelay(portMAX_DELAY); }