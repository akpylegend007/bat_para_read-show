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

// ============================================================================
// 1. BLE & BMS TELEMETRY DATA LAYER (PROTECTED)
// ============================================================================
static const char *BATTERY_NAME_MATCH = "CE024AA005260138";

struct Telemetry {
  uint32_t timestampMs = 0;
  float voltage = 0.0f;
  float current = 0.0f;
  float remainingAh = 0.0f;
  float designAh = 0.0f;
  uint16_t cycles = 0;
  int8_t temperatureC = 0;
  uint16_t protection = 0;
  uint32_t balanceMaskLow = 0, balanceMaskHigh = 0;
  uint8_t soc = 0;
  bool chargeFet = false, dischargeFet = false;
  int8_t rssi = 0;
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

// ============================================================================
// 2. DESIGN TOKENS & PALETTE
// ============================================================================
#define COLOR_BG_PRIMARY       lv_color_hex(0x0B0E17)
#define COLOR_BG_CARD          lv_color_hex(0x151A2A)
#define COLOR_BG_CARD_ELEVATED lv_color_hex(0x1C2338)
#define COLOR_ACCENT_GOLD      lv_color_hex(0xFFD700)
#define COLOR_ACCENT_CYAN      lv_color_hex(0x00E5FF)
#define COLOR_ACCENT_GREEN     lv_color_hex(0x00E676)
#define COLOR_ACCENT_ORANGE    lv_color_hex(0xFF9100)
#define COLOR_ACCENT_RED       lv_color_hex(0xFF1744)
#define COLOR_TEXT_PRIMARY     lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY   lv_color_hex(0x8892A8)
#define COLOR_TEXT_DISABLED    lv_color_hex(0x4A5268)
#define COLOR_DIVIDER          lv_color_hex(0x1E2640)
#define COLOR_GAUGE_TRACK      lv_color_hex(0x1A2236)
#define COLOR_CELL_OK          lv_color_hex(0x00E676)
#define COLOR_CELL_WARN        lv_color_hex(0xFFD700)
#define COLOR_CELL_CRITICAL    lv_color_hex(0xFF1744)

// ============================================================================
// 3. UI OBJECT DEFINITIONS
// ============================================================================
// Screens
static lv_obj_t *splash_scr = nullptr;
static lv_obj_t *main_scr   = nullptr;
static lv_obj_t *cells_scr  = nullptr;
static lv_obj_t *health_scr = nullptr;

// Splash Screen Elements
static lv_obj_t *splash_bar = nullptr;
static lv_obj_t *splash_status_label = nullptr;

// Screen 1: Dashboard Elements
static lv_obj_t *s1_conn_dot = nullptr;
static lv_obj_t *s1_temp_label = nullptr;
static lv_obj_t *s1_soc_arc = nullptr;
static lv_obj_t *s1_soc_val_label = nullptr;
static lv_obj_t *s1_status_pill = nullptr;
static lv_obj_t *s1_status_label = nullptr;
static lv_obj_t *s1_volt_val_label = nullptr;
static lv_obj_t *s1_curr_val_label = nullptr;

// Screen 2: Cell Diagnostics Elements
static lv_obj_t *s2_delta_label = nullptr;
static lv_obj_t *s2_cell_cards[16] = {nullptr};
static lv_obj_t *s2_cell_num_labels[16] = {nullptr};
static lv_obj_t *s2_cell_volt_labels[16] = {nullptr};
static lv_obj_t *s2_cell_bars[16] = {nullptr};
static lv_obj_t *s2_summary_card = nullptr;
static lv_obj_t *s2_min_label = nullptr;
static lv_obj_t *s2_max_label = nullptr;
static lv_obj_t *s2_delta_summary_label = nullptr;

// Screen 3: System Health Elements
static lv_obj_t *s3_cap_val_label = nullptr;
static lv_obj_t *s3_cap_bar = nullptr;
static lv_obj_t *s3_cycles_val_label = nullptr;
static lv_obj_t *s3_temp_val_label = nullptr;
static lv_obj_t *s3_alarm_cards[5] = {nullptr};
static lv_obj_t *s3_alarm_status_labels[5] = {nullptr};
static lv_obj_t *s3_fet_chg_label = nullptr;
static lv_obj_t *s3_fet_dchg_label = nullptr;

// ============================================================================
// 4. NAVIGATION & GESTURE HANDLERS
// ============================================================================
static void load_screen_with_anim(lv_obj_t *target, lv_scr_load_anim_t anim_type) {
  if (target && lv_scr_act() != target) {
    lv_scr_load_anim(target, anim_type, 250, 0, false);
  }
}

static void screen_gesture_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    lv_obj_t *act = lv_scr_act();
    if (dir == LV_DIR_LEFT) {
      if (act == main_scr) load_screen_with_anim(cells_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT);
      else if (act == cells_scr) load_screen_with_anim(health_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT);
      else if (act == health_scr) load_screen_with_anim(main_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    } else if (dir == LV_DIR_RIGHT) {
      if (act == main_scr) load_screen_with_anim(health_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
      else if (act == cells_scr) load_screen_with_anim(main_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
      else if (act == health_scr) load_screen_with_anim(cells_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
  }
}

static void nav_tap_event_cb(lv_event_t * e) {
  lv_obj_t *act = lv_scr_act();
  uintptr_t target_idx = (uintptr_t)lv_event_get_user_data(e);
  if (target_idx == 0) load_screen_with_anim(main_scr, LV_SCR_LOAD_ANIM_FADE_ON);
  else if (target_idx == 1) load_screen_with_anim(cells_scr, LV_SCR_LOAD_ANIM_FADE_ON);
  else if (target_idx == 2) load_screen_with_anim(health_scr, LV_SCR_LOAD_ANIM_FADE_ON);
}

static void create_page_dots(lv_obj_t *parent, uint8_t active_idx) {
  lv_obj_t *dot_cont = lv_obj_create(parent);
  lv_obj_set_size(dot_cont, 140, 24);
  lv_obj_align(dot_cont, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_bg_opa(dot_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(dot_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(dot_cont, 0, 0);
  lv_obj_clear_flag(dot_cont, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < 3; i++) {
    lv_obj_t *dot = lv_btn_create(dot_cont);
    lv_obj_set_size(dot, (i == active_idx) ? 14 : 8, 8);
    lv_obj_align(dot, LV_ALIGN_CENTER, (i - 1) * 22, 0);
    lv_obj_set_style_radius(dot, 4, 0);
    lv_obj_set_style_bg_color(dot, (i == active_idx) ? COLOR_ACCENT_GOLD : COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_add_event_cb(dot, nav_tap_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
}

// ============================================================================
// 5. SCREEN BUILDERS
// ============================================================================

// --- 5.1 BOOT SPLASH SCREEN ---
static void build_splash_screen() {
  splash_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(splash_scr, COLOR_BG_PRIMARY, 0);
  lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(splash_scr, LV_OBJ_FLAG_SCROLLABLE);

  // Battery Outline Icon
  lv_obj_t *bat_body = lv_obj_create(splash_scr);
  lv_obj_set_size(bat_body, 46, 68);
  lv_obj_align(bat_body, LV_ALIGN_CENTER, 0, -45);
  lv_obj_set_style_bg_opa(bat_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(bat_body, COLOR_ACCENT_GOLD, 0);
  lv_obj_set_style_border_width(bat_body, 3, 0);
  lv_obj_set_style_radius(bat_body, 6, 0);

  lv_obj_t *bat_cap = lv_obj_create(splash_scr);
  lv_obj_set_size(bat_cap, 18, 6);
  lv_obj_align(bat_cap, LV_ALIGN_CENTER, 0, -82);
  lv_obj_set_style_bg_color(bat_cap, COLOR_ACCENT_GOLD, 0);
  lv_obj_set_style_bg_opa(bat_cap, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(bat_cap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(bat_cap, 2, 0);

  // Title
  lv_obj_t *title = lv_label_create(splash_scr);
  lv_label_set_text(title, "LFPGo400");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 15);

  // Animated Loading Bar
  splash_bar = lv_bar_create(splash_scr);
  lv_obj_set_size(splash_bar, 120, 6);
  lv_obj_align(splash_bar, LV_ALIGN_CENTER, 0, 48);
  lv_obj_set_style_bg_color(splash_bar, COLOR_DIVIDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(splash_bar, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_radius(splash_bar, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(splash_bar, 3, LV_PART_INDICATOR);
  lv_bar_set_range(splash_bar, 0, 100);
  lv_bar_set_value(splash_bar, 0, LV_ANIM_OFF);

  // Animate the bar value
  lv_anim_t a_bar;
  lv_anim_init(&a_bar);
  lv_anim_set_var(&a_bar, splash_bar);
  lv_anim_set_values(&a_bar, 0, 100);
  lv_anim_set_time(&a_bar, 2000);
  lv_anim_set_playback_time(&a_bar, 0);
  lv_anim_set_repeat_count(&a_bar, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&a_bar, (lv_anim_exec_xcb_t)lv_bar_set_value);
  lv_anim_start(&a_bar);

  // Status Label
  splash_status_label = lv_label_create(splash_scr);
  lv_label_set_text(splash_status_label, "SCANNING FOR BMS...");
  lv_obj_set_style_text_font(splash_status_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(splash_status_label, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(splash_status_label, LV_ALIGN_CENTER, 0, 78);

  // Animate the status label opacity
  lv_anim_t a_opa;
  lv_anim_init(&a_opa);
  lv_anim_set_var(&a_opa, splash_status_label);
  lv_anim_set_values(&a_opa, LV_OPA_30, LV_OPA_COVER);
  lv_anim_set_time(&a_opa, 750);
  lv_anim_set_playback_time(&a_opa, 750);
  lv_anim_set_repeat_count(&a_opa, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a_opa, (lv_anim_exec_xcb_t)lv_obj_set_style_text_opa);
  lv_anim_start(&a_opa);
}

// --- 5.2 MAIN DASHBOARD SCREEN ---
static void build_main_screen() {
  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, COLOR_BG_PRIMARY, 0);
  lv_obj_set_style_bg_opa(main_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(main_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(main_scr, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  // Status Bar (Top)
  s1_conn_dot = lv_obj_create(main_scr);
  lv_obj_set_size(s1_conn_dot, 8, 8);
  lv_obj_align(s1_conn_dot, LV_ALIGN_TOP_LEFT, 14, 10);
  lv_obj_set_style_radius(s1_conn_dot, 4, 0);
  lv_obj_set_style_bg_color(s1_conn_dot, COLOR_ACCENT_RED, 0);
  lv_obj_set_style_border_opa(s1_conn_dot, LV_OPA_TRANSP, 0);

  lv_obj_t *name_lbl = lv_label_create(main_scr);
  lv_label_set_text(name_lbl, "LFPGo400");
  lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(name_lbl, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(name_lbl, LV_ALIGN_TOP_MID, 0, 8);

  s1_temp_label = lv_label_create(main_scr);
  lv_label_set_text(s1_temp_label, "--\xc2\xb0\x43");
  lv_obj_set_style_text_font(s1_temp_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s1_temp_label, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(s1_temp_label, LV_ALIGN_TOP_RIGHT, -14, 8);

  // SOC Ring Gauge (160x160 px, 270 deg sweep)
  s1_soc_arc = lv_arc_create(main_scr);
  lv_obj_set_size(s1_soc_arc, 160, 160);
  lv_obj_align(s1_soc_arc, LV_ALIGN_CENTER, 0, -42);
  lv_arc_set_rotation(s1_soc_arc, 135);
  lv_arc_set_bg_angles(s1_soc_arc, 0, 270);
  lv_arc_set_range(s1_soc_arc, 0, 100);
  lv_arc_set_value(s1_soc_arc, 0);
  lv_obj_remove_style(s1_soc_arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(s1_soc_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(s1_soc_arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s1_soc_arc, COLOR_GAUGE_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s1_soc_arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s1_soc_arc, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);

  // Center Value Inside Arc
  s1_soc_val_label = lv_label_create(main_scr);
  lv_label_set_text(s1_soc_val_label, "--%");
  lv_obj_set_style_text_font(s1_soc_val_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(s1_soc_val_label, COLOR_TEXT_PRIMARY, 0);
  lv_obj_align(s1_soc_val_label, LV_ALIGN_CENTER, 0, -46);

  lv_obj_t *soc_sub = lv_label_create(main_scr);
  lv_label_set_text(soc_sub, "STATE OF CHARGE");
  lv_obj_set_style_text_font(soc_sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(soc_sub, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(soc_sub, LV_ALIGN_CENTER, 0, -18);

  // Status Pill
  s1_status_pill = lv_obj_create(main_scr);
  lv_obj_set_size(s1_status_pill, 160, 28);
  lv_obj_align(s1_status_pill, LV_ALIGN_CENTER, 0, 52);
  lv_obj_set_style_radius(s1_status_pill, 14, 0);
  lv_obj_set_style_bg_color(s1_status_pill, COLOR_DIVIDER, 0);
  lv_obj_set_style_border_opa(s1_status_pill, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s1_status_pill, LV_OBJ_FLAG_SCROLLABLE);

  s1_status_label = lv_label_create(s1_status_pill);
  lv_label_set_text(s1_status_label, "CONNECTING...");
  lv_obj_set_style_text_font(s1_status_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s1_status_label, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(s1_status_label, LV_ALIGN_CENTER, 0, 0);

  // Dual Cards Row (Voltage / Current)
  lv_obj_t *v_card = lv_obj_create(main_scr);
  lv_obj_set_size(v_card, 104, 62);
  lv_obj_align(v_card, LV_ALIGN_TOP_LEFT, 12, 226);
  lv_obj_set_style_bg_color(v_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(v_card, 8, 0);
  lv_obj_set_style_border_opa(v_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(v_card, LV_OBJ_FLAG_SCROLLABLE);

  s1_volt_val_label = lv_label_create(v_card);
  lv_label_set_text(s1_volt_val_label, "--.- V");
  lv_obj_set_style_text_font(s1_volt_val_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s1_volt_val_label, COLOR_ACCENT_CYAN, 0);
  lv_obj_align(s1_volt_val_label, LV_ALIGN_CENTER, 0, -8);

  lv_obj_t *v_sub = lv_label_create(v_card);
  lv_label_set_text(v_sub, "VOLTAGE");
  lv_obj_set_style_text_font(v_sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(v_sub, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(v_sub, LV_ALIGN_CENTER, 0, 14);

  lv_obj_t *i_card = lv_obj_create(main_scr);
  lv_obj_set_size(i_card, 104, 62);
  lv_obj_align(i_card, LV_ALIGN_TOP_RIGHT, -12, 226);
  lv_obj_set_style_bg_color(i_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(i_card, 8, 0);
  lv_obj_set_style_border_opa(i_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(i_card, LV_OBJ_FLAG_SCROLLABLE);

  s1_curr_val_label = lv_label_create(i_card);
  lv_label_set_text(s1_curr_val_label, "--.- A");
  lv_obj_set_style_text_font(s1_curr_val_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s1_curr_val_label, COLOR_ACCENT_GREEN, 0);
  lv_obj_align(s1_curr_val_label, LV_ALIGN_CENTER, 0, -8);

  lv_obj_t *i_sub = lv_label_create(i_card);
  lv_label_set_text(i_sub, "CURRENT");
  lv_obj_set_style_text_font(i_sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(i_sub, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(i_sub, LV_ALIGN_CENTER, 0, 14);

  create_page_dots(main_scr, 0);
}

// --- 5.3 CELL DIAGNOSTICS SCREEN ---
static void build_cells_screen() {
  cells_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(cells_scr, COLOR_BG_PRIMARY, 0);
  lv_obj_set_style_bg_opa(cells_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(cells_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(cells_scr, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  // Title Bar
  lv_obj_t *title = lv_label_create(cells_scr);
  lv_label_set_text(title, "CELL VOLTAGES");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

  s2_delta_label = lv_label_create(cells_scr);
  lv_label_set_text(s2_delta_label, "\xce\x94 --mV");
  lv_obj_set_style_text_font(s2_delta_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s2_delta_label, COLOR_ACCENT_GREEN, 0);
  lv_obj_align(s2_delta_label, LV_ALIGN_TOP_RIGHT, -12, 8);

  // Cell Grid (4 Columns x 4 Rows = 16 Max Cells)
  for (int i = 0; i < 16; i++) {
    int col = i % 4;
    int row = i / 4;
    int x_pos = 12 + col * 55;
    int y_pos = 32 + row * 46;

    s2_cell_cards[i] = lv_obj_create(cells_scr);
    lv_obj_set_size(s2_cell_cards[i], 50, 42);
    lv_obj_set_pos(s2_cell_cards[i], x_pos, y_pos);
    lv_obj_set_style_bg_color(s2_cell_cards[i], COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(s2_cell_cards[i], 4, 0);
    lv_obj_set_style_border_opa(s2_cell_cards[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s2_cell_cards[i], 0, 0);
    lv_obj_clear_flag(s2_cell_cards[i], LV_OBJ_FLAG_SCROLLABLE);

    s2_cell_num_labels[i] = lv_label_create(s2_cell_cards[i]);
    char buf[8];
    snprintf(buf, sizeof(buf), "C%d", i + 1);
    lv_label_set_text(s2_cell_num_labels[i], buf);
    lv_obj_set_style_text_font(s2_cell_num_labels[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s2_cell_num_labels[i], COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(s2_cell_num_labels[i], LV_ALIGN_TOP_LEFT, 3, 2);

    s2_cell_volt_labels[i] = lv_label_create(s2_cell_cards[i]);
    lv_label_set_text(s2_cell_volt_labels[i], "----");
    lv_obj_set_style_text_font(s2_cell_volt_labels[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s2_cell_volt_labels[i], COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s2_cell_volt_labels[i], LV_ALIGN_CENTER, 0, -1);

    s2_cell_bars[i] = lv_bar_create(s2_cell_cards[i]);
    lv_obj_set_size(s2_cell_bars[i], 42, 3);
    lv_obj_align(s2_cell_bars[i], LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_bar_set_range(s2_cell_bars[i], 2800, 3650);
    lv_bar_set_value(s2_cell_bars[i], 3300, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s2_cell_bars[i], COLOR_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s2_cell_bars[i], COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s2_cell_bars[i], 1, LV_PART_INDICATOR);
  }

  // Summary Card (Bottom)
  s2_summary_card = lv_obj_create(cells_scr);
  lv_obj_set_size(s2_summary_card, 216, 52);
  lv_obj_align(s2_summary_card, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_set_style_bg_color(s2_summary_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(s2_summary_card, 6, 0);
  lv_obj_set_style_border_opa(s2_summary_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s2_summary_card, LV_OBJ_FLAG_SCROLLABLE);

  s2_min_label = lv_label_create(s2_summary_card);
  lv_label_set_text(s2_min_label, "MIN: ---- mV (--)");
  lv_obj_set_style_text_font(s2_min_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s2_min_label, COLOR_CELL_WARN, 0);
  lv_obj_align(s2_min_label, LV_ALIGN_TOP_LEFT, 6, 4);

  s2_max_label = lv_label_create(s2_summary_card);
  lv_label_set_text(s2_max_label, "MAX: ---- mV (--)");
  lv_obj_set_style_text_font(s2_max_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s2_max_label, COLOR_ACCENT_CYAN, 0);
  lv_obj_align(s2_max_label, LV_ALIGN_TOP_RIGHT, -6, 4);

  s2_delta_summary_label = lv_label_create(s2_summary_card);
  lv_label_set_text(s2_delta_summary_label, "DELTA: -- mV");
  lv_obj_set_style_text_font(s2_delta_summary_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s2_delta_summary_label, COLOR_ACCENT_GREEN, 0);
  lv_obj_align(s2_delta_summary_label, LV_ALIGN_BOTTOM_MID, 0, -4);

  create_page_dots(cells_scr, 1);
}

// --- 5.4 SYSTEM HEALTH SCREEN ---
static void build_health_screen() {
  health_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(health_scr, COLOR_BG_PRIMARY, 0);
  lv_obj_set_style_bg_opa(health_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(health_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(health_scr, screen_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  // Title Bar
  lv_obj_t *title = lv_label_create(health_scr);
  lv_label_set_text(title, "SYSTEM HEALTH");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

  // Capacity Card
  lv_obj_t *cap_card = lv_obj_create(health_scr);
  lv_obj_set_size(cap_card, 216, 56);
  lv_obj_align(cap_card, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_bg_color(cap_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(cap_card, 8, 0);
  lv_obj_set_style_border_opa(cap_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(cap_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cap_title = lv_label_create(cap_card);
  lv_label_set_text(cap_title, "CAPACITY");
  lv_obj_set_style_text_font(cap_title, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap_title, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(cap_title, LV_ALIGN_TOP_LEFT, 8, 4);

  s3_cap_val_label = lv_label_create(cap_card);
  lv_label_set_text(s3_cap_val_label, "--.- / --.- Ah");
  lv_obj_set_style_text_font(s3_cap_val_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s3_cap_val_label, COLOR_ACCENT_CYAN, 0);
  lv_obj_align(s3_cap_val_label, LV_ALIGN_TOP_RIGHT, -8, 4);

  s3_cap_bar = lv_bar_create(cap_card);
  lv_obj_set_size(s3_cap_bar, 196, 6);
  lv_obj_align(s3_cap_bar, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_bar_set_range(s3_cap_bar, 0, 100);
  lv_bar_set_value(s3_cap_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s3_cap_bar, COLOR_DIVIDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s3_cap_bar, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s3_cap_bar, 3, LV_PART_INDICATOR);

  // Dual Metric Cards (Cycles / Temp)
  lv_obj_t *cyc_card = lv_obj_create(health_scr);
  lv_obj_set_size(cyc_card, 104, 56);
  lv_obj_align(cyc_card, LV_ALIGN_TOP_LEFT, 12, 96);
  lv_obj_set_style_bg_color(cyc_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(cyc_card, 8, 0);
  lv_obj_set_style_border_opa(cyc_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(cyc_card, LV_OBJ_FLAG_SCROLLABLE);

  s3_cycles_val_label = lv_label_create(cyc_card);
  lv_label_set_text(s3_cycles_val_label, "---");
  lv_obj_set_style_text_font(s3_cycles_val_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s3_cycles_val_label, COLOR_ACCENT_CYAN, 0);
  lv_obj_align(s3_cycles_val_label, LV_ALIGN_CENTER, 0, -6);

  lv_obj_t *cyc_sub = lv_label_create(cyc_card);
  lv_label_set_text(cyc_sub, "CYCLES");
  lv_obj_set_style_text_font(cyc_sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cyc_sub, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(cyc_sub, LV_ALIGN_CENTER, 0, 14);

  lv_obj_t *tmp_card = lv_obj_create(health_scr);
  lv_obj_set_size(tmp_card, 104, 56);
  lv_obj_align(tmp_card, LV_ALIGN_TOP_RIGHT, -12, 96);
  lv_obj_set_style_bg_color(tmp_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(tmp_card, 8, 0);
  lv_obj_set_style_border_opa(tmp_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(tmp_card, LV_OBJ_FLAG_SCROLLABLE);

  s3_temp_val_label = lv_label_create(tmp_card);
  lv_label_set_text(s3_temp_val_label, "-- \xc2\xb0\x43");
  lv_obj_set_style_text_font(s3_temp_val_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s3_temp_val_label, COLOR_ACCENT_GREEN, 0);
  lv_obj_align(s3_temp_val_label, LV_ALIGN_CENTER, 0, -6);

  lv_obj_t *tmp_sub = lv_label_create(tmp_card);
  lv_label_set_text(tmp_sub, "PACK TEMP");
  lv_obj_set_style_text_font(tmp_sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tmp_sub, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(tmp_sub, LV_ALIGN_CENTER, 0, 14);

  // Protection Alarms Grid (5 indicators: OV, UV, OC, SC, OT)
  lv_obj_t *alarm_header = lv_label_create(health_scr);
  lv_label_set_text(alarm_header, "PROTECTION STATUS");
  lv_obj_set_style_text_font(alarm_header, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(alarm_header, COLOR_TEXT_SECONDARY, 0);
  lv_obj_align(alarm_header, LV_ALIGN_TOP_LEFT, 12, 160);

  const char *alarm_names[5] = {"OV", "UV", "OC", "SC", "OT"};
  for (int i = 0; i < 5; i++) {
    s3_alarm_cards[i] = lv_obj_create(health_scr);
    lv_obj_set_size(s3_alarm_cards[i], 38, 38);
    lv_obj_set_pos(s3_alarm_cards[i], 12 + i * 44, 180);
    lv_obj_set_style_bg_color(s3_alarm_cards[i], COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(s3_alarm_cards[i], 4, 0);
    lv_obj_set_style_border_opa(s3_alarm_cards[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s3_alarm_cards[i], 0, 0);
    lv_obj_clear_flag(s3_alarm_cards[i], LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_name = lv_label_create(s3_alarm_cards[i]);
    lv_label_set_text(lbl_name, alarm_names[i]);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_name, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 2);

    s3_alarm_status_labels[i] = lv_label_create(s3_alarm_cards[i]);
    lv_label_set_text(s3_alarm_status_labels[i], "OK");
    lv_obj_set_style_text_font(s3_alarm_status_labels[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s3_alarm_status_labels[i], COLOR_ACCENT_GREEN, 0);
    lv_obj_align(s3_alarm_status_labels[i], LV_ALIGN_BOTTOM_MID, 0, -2);
  }

  // FET Relay Status Card
  lv_obj_t *fet_card = lv_obj_create(health_scr);
  lv_obj_set_size(fet_card, 216, 40);
  lv_obj_align(fet_card, LV_ALIGN_TOP_MID, 0, 228);
  lv_obj_set_style_bg_color(fet_card, COLOR_BG_CARD, 0);
  lv_obj_set_style_radius(fet_card, 8, 0);
  lv_obj_set_style_border_opa(fet_card, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(fet_card, LV_OBJ_FLAG_SCROLLABLE);

  s3_fet_chg_label = lv_label_create(fet_card);
  lv_label_set_text(s3_fet_chg_label, "CHG: --");
  lv_obj_set_style_text_font(s3_fet_chg_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s3_fet_chg_label, COLOR_ACCENT_GREEN, 0);
  lv_obj_align(s3_fet_chg_label, LV_ALIGN_LEFT_MID, 8, 0);

  s3_fet_dchg_label = lv_label_create(fet_card);
  lv_label_set_text(s3_fet_dchg_label, "DCHG: --");
  lv_obj_set_style_text_font(s3_fet_dchg_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s3_fet_dchg_label, COLOR_ACCENT_GREEN, 0);
  lv_obj_align(s3_fet_dchg_label, LV_ALIGN_RIGHT_MID, -8, 0);

  create_page_dots(health_scr, 2);
}

// ============================================================================
// 6. DYNAMIC DATA BINDING & ANIMATION UPDATER
// ============================================================================
static void updateUI(const Telemetry &d) {
  static bool prev_connected = false;
  static uint32_t last_anim_ms = 0;
  uint32_t now = millis();

  // Handle Splash Screen Transition to Main Screen on first successful data reception
  if (d.timestampMs > 0 && batteryConnected) {
    if (lv_scr_act() == splash_scr) {
      lv_scr_load_anim(main_scr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    }
  }

  // --- SCREEN 1: MAIN DASHBOARD ---
  if (main_scr) {
    // Connection Dot
    if (s1_conn_dot) {
      if (batteryConnected) {
        lv_obj_set_style_bg_color(s1_conn_dot, COLOR_ACCENT_GREEN, 0);
      } else {
        lv_obj_set_style_bg_color(s1_conn_dot, COLOR_ACCENT_RED, 0);
      }
    }

    // Temperature (Top Bar)
    if (s1_temp_label) {
      if (d.timestampMs > 0) {
        lv_label_set_text_fmt(s1_temp_label, "%d\xc2\xb0\x43", d.temperatureC);
        if (d.temperatureC > 45) lv_obj_set_style_text_color(s1_temp_label, COLOR_ACCENT_RED, 0);
        else if (d.temperatureC > 35) lv_obj_set_style_text_color(s1_temp_label, COLOR_ACCENT_ORANGE, 0);
        else lv_obj_set_style_text_color(s1_temp_label, COLOR_TEXT_SECONDARY, 0);
      } else {
        lv_label_set_text(s1_temp_label, "--\xc2\xb0\x43");
      }
    }

    // SOC Gauge & Arc
    if (s1_soc_val_label && s1_soc_arc) {
      if (d.timestampMs > 0) {
        lv_label_set_text_fmt(s1_soc_val_label, "%u%%", d.soc);
        lv_arc_set_value(s1_soc_arc, d.soc);
        if (d.soc >= 50) {
          lv_obj_set_style_arc_color(s1_soc_arc, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
        } else if (d.soc >= 20) {
          lv_obj_set_style_arc_color(s1_soc_arc, COLOR_ACCENT_GOLD, LV_PART_INDICATOR);
        } else {
          lv_obj_set_style_arc_color(s1_soc_arc, COLOR_ACCENT_RED, LV_PART_INDICATOR);
        }
      } else {
        lv_label_set_text(s1_soc_val_label, "--%");
        lv_arc_set_value(s1_soc_arc, 0);
      }
    }

    // Status Pill
    if (s1_status_pill && s1_status_label) {
      if (!batteryConnected || d.timestampMs == 0) {
        lv_obj_set_style_bg_color(s1_status_pill, lv_color_hex(0x2A1015), 0);
        lv_obj_set_style_text_color(s1_status_label, COLOR_ACCENT_RED, 0);
        lv_label_set_text(s1_status_label, "X DISCONNECTED");
      } else if (d.current > 0.05f) {
        lv_obj_set_style_bg_color(s1_status_pill, lv_color_hex(0x0C2818), 0);
        lv_obj_set_style_text_color(s1_status_label, COLOR_ACCENT_GREEN, 0);
        lv_label_set_text(s1_status_label, "+ CHARGING");
      } else if (d.current < -0.05f) {
        lv_obj_set_style_bg_color(s1_status_pill, lv_color_hex(0x281C0C), 0);
        lv_obj_set_style_text_color(s1_status_label, COLOR_ACCENT_ORANGE, 0);
        lv_label_set_text(s1_status_label, "- DISCHARGING");
      } else {
        lv_obj_set_style_bg_color(s1_status_pill, COLOR_DIVIDER, 0);
        lv_obj_set_style_text_color(s1_status_label, COLOR_TEXT_SECONDARY, 0);
        lv_label_set_text(s1_status_label, "- IDLE");
      }
    }

    // Voltage & Current
    if (s1_volt_val_label) {
      if (d.timestampMs > 0) lv_label_set_text_fmt(s1_volt_val_label, "%.1f V", d.voltage);
      else lv_label_set_text(s1_volt_val_label, "--.- V");
    }

    if (s1_curr_val_label) {
      if (d.timestampMs > 0) {
        lv_label_set_text_fmt(s1_curr_val_label, "%+.1f A", d.current);
        if (d.current > 0.05f) lv_obj_set_style_text_color(s1_curr_val_label, COLOR_ACCENT_GREEN, 0);
        else if (d.current < -0.05f) lv_obj_set_style_text_color(s1_curr_val_label, COLOR_ACCENT_ORANGE, 0);
        else lv_obj_set_style_text_color(s1_curr_val_label, COLOR_ACCENT_CYAN, 0);
      } else {
        lv_label_set_text(s1_curr_val_label, "--.- A");
      }
    }
  }

  // --- SCREEN 2: CELL DIAGNOSTICS ---
  if (cells_scr && d.cellCount > 0) {
    uint16_t min_v = 65535, max_v = 0;
    uint8_t min_idx = 0, max_idx = 0;

    for (uint8_t i = 0; i < d.cellCount && i < 16; i++) {
      uint16_t v = d.cellVoltages[i];
      if (v > 0) {
        if (v < min_v) { min_v = v; min_idx = i + 1; }
        if (v > max_v) { max_v = v; max_idx = i + 1; }
      }
    }
    uint16_t delta_v = (max_v >= min_v) ? (max_v - min_v) : 0;

    // Title Delta
    if (s2_delta_label) {
      lv_label_set_text_fmt(s2_delta_label, "\xce\x94 %u mV", delta_v);
      if (delta_v > 50) lv_obj_set_style_text_color(s2_delta_label, COLOR_CELL_CRITICAL, 0);
      else if (delta_v > 20) lv_obj_set_style_text_color(s2_delta_label, COLOR_CELL_WARN, 0);
      else lv_obj_set_style_text_color(s2_delta_label, COLOR_CELL_OK, 0);
    }

    // Cell Cards
    for (uint8_t i = 0; i < 16; i++) {
      if (i < d.cellCount) {
        lv_obj_clear_flag(s2_cell_cards[i], LV_OBJ_FLAG_HIDDEN);
        uint16_t v = d.cellVoltages[i];
        lv_label_set_text_fmt(s2_cell_volt_labels[i], "%u", v);
        lv_bar_set_value(s2_cell_bars[i], v, LV_ANIM_ON);

        // Dynamic Card / Bar Color
        if (v < 2900 || v > 3650) {
          lv_obj_set_style_bg_color(s2_cell_bars[i], COLOR_CELL_CRITICAL, LV_PART_INDICATOR);
          lv_obj_set_style_text_color(s2_cell_volt_labels[i], COLOR_CELL_CRITICAL, 0);
        } else if (i + 1 == min_idx) {
          lv_obj_set_style_bg_color(s2_cell_bars[i], COLOR_CELL_WARN, LV_PART_INDICATOR);
          lv_obj_set_style_text_color(s2_cell_volt_labels[i], COLOR_CELL_WARN, 0);
        } else if (i + 1 == max_idx) {
          lv_obj_set_style_bg_color(s2_cell_bars[i], COLOR_ACCENT_CYAN, LV_PART_INDICATOR);
          lv_obj_set_style_text_color(s2_cell_volt_labels[i], COLOR_ACCENT_CYAN, 0);
        } else {
          lv_obj_set_style_bg_color(s2_cell_bars[i], COLOR_CELL_OK, LV_PART_INDICATOR);
          lv_obj_set_style_text_color(s2_cell_volt_labels[i], COLOR_TEXT_PRIMARY, 0);
        }
      } else {
        lv_obj_add_flag(s2_cell_cards[i], LV_OBJ_FLAG_HIDDEN);
      }
    }

    // Summary Card
    if (s2_min_label) lv_label_set_text_fmt(s2_min_label, "MIN: %u mV (C%u)", min_v, min_idx);
    if (s2_max_label) lv_label_set_text_fmt(s2_max_label, "MAX: %u mV (C%u)", max_v, max_idx);
    if (s2_delta_summary_label) {
      lv_label_set_text_fmt(s2_delta_summary_label, "DELTA: %u mV", delta_v);
      if (delta_v > 50) lv_obj_set_style_text_color(s2_delta_summary_label, COLOR_CELL_CRITICAL, 0);
      else if (delta_v > 20) lv_obj_set_style_text_color(s2_delta_summary_label, COLOR_CELL_WARN, 0);
      else lv_obj_set_style_text_color(s2_delta_summary_label, COLOR_CELL_OK, 0);
    }
  }

  // --- SCREEN 3: SYSTEM HEALTH ---
  if (health_scr && d.timestampMs > 0) {
    if (s3_cap_val_label) lv_label_set_text_fmt(s3_cap_val_label, "%.1f / %.1f Ah", d.remainingAh, d.designAh);
    if (s3_cap_bar && d.designAh > 0) {
      uint8_t cap_pct = (uint8_t)((d.remainingAh / d.designAh) * 100.0f);
      lv_bar_set_value(s3_cap_bar, cap_pct, LV_ANIM_ON);
      if (cap_pct >= 50) lv_obj_set_style_bg_color(s3_cap_bar, COLOR_ACCENT_GREEN, LV_PART_INDICATOR);
      else if (cap_pct >= 20) lv_obj_set_style_bg_color(s3_cap_bar, COLOR_ACCENT_GOLD, LV_PART_INDICATOR);
      else lv_obj_set_style_bg_color(s3_cap_bar, COLOR_ACCENT_RED, LV_PART_INDICATOR);
    }

    if (s3_cycles_val_label) lv_label_set_text_fmt(s3_cycles_val_label, "%u", d.cycles);
    if (s3_temp_val_label) {
      lv_label_set_text_fmt(s3_temp_val_label, "%d \xc2\xb0\x43", d.temperatureC);
      if (d.temperatureC > 45) lv_obj_set_style_text_color(s3_temp_val_label, COLOR_ACCENT_RED, 0);
      else if (d.temperatureC > 35) lv_obj_set_style_text_color(s3_temp_val_label, COLOR_ACCENT_ORANGE, 0);
      else lv_obj_set_style_text_color(s3_temp_val_label, COLOR_ACCENT_GREEN, 0);
    }

    // Protection Alarm Grid (Bits 0..4)
    for (int i = 0; i < 5; i++) {
      bool tripped = (d.protection & (1 << i));
      if (tripped) {
        lv_label_set_text(s3_alarm_status_labels[i], "TRIP");
        lv_obj_set_style_text_color(s3_alarm_status_labels[i], COLOR_ACCENT_RED, 0);
        lv_obj_set_style_bg_color(s3_alarm_cards[i], lv_color_hex(0x351015), 0);
      } else {
        lv_label_set_text(s3_alarm_status_labels[i], "OK");
        lv_obj_set_style_text_color(s3_alarm_status_labels[i], COLOR_ACCENT_GREEN, 0);
        lv_obj_set_style_bg_color(s3_alarm_cards[i], COLOR_BG_CARD, 0);
      }
    }

    // FET Status
    if (s3_fet_chg_label) {
      lv_label_set_text(s3_fet_chg_label, d.chargeFet ? "CHG: ON" : "CHG: OFF");
      lv_obj_set_style_text_color(s3_fet_chg_label, d.chargeFet ? COLOR_ACCENT_GREEN : COLOR_ACCENT_RED, 0);
    }
    if (s3_fet_dchg_label) {
      lv_label_set_text(s3_fet_dchg_label, d.dischargeFet ? "DCHG: ON" : "DCHG: OFF");
      lv_obj_set_style_text_color(s3_fet_dchg_label, d.dischargeFet ? COLOR_ACCENT_GREEN : COLOR_ACCENT_RED, 0);
    }
  }
}

// ============================================================================
// 7. JBD BMS PROTOCOL PARSER (PROTECTED)
// ============================================================================
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

// ============================================================================
// 8. FREERTOS TASK LOOPS
// ============================================================================
static void logicTask(void *) {
  uint32_t lastConnectMs = 0, lastPollMs = 0;
  for (;;) {
    uint32_t now = millis();
    if ((!client || !client->isConnected()) && now - lastConnectMs >= 6000) { 
      lastConnectMs = now; 
      findAndConnectBattery(); 
    }
    if (batteryConnected && now - lastPollMs >= 250) { 
      lastPollMs = now; 
      pollBattery(); 
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void uiTask(void *) {
  for (;;) {
    Lvgl_Loop();
    updateUI(snapshotTelemetry());
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ============================================================================
// 9. ARDUINO SETUP & SYSTEM INIT
// ============================================================================
void setup() {
  Serial.begin(115200);
  telemetryMutex = xSemaphoreCreateMutex();
  
  // Power latching & power key
  pinMode(6, INPUT);  // PWR_KEY_INPUT
  pinMode(7, OUTPUT); // PWR_CONTROL
  digitalWrite(7, HIGH);
  
  // Hardware Initialization
  LCD_Init();
  Touch_Init();
  Backlight_Init();
  Set_Backlight(50); // 50% Duty cycle for thermal efficiency
  
  // Initialize LVGL 8.3 Driver
  Lvgl_Init();
  
  // Construct All Screen Interfaces
  build_splash_screen();
  build_main_screen();
  build_cells_screen();
  build_health_screen();
  
  // Initial Display: Boot Splash Screen
  lv_scr_load(splash_scr);

  // Initialize NimBLE Stack
  NimBLEDevice::init("LFPGo400 Dashboard");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  // Launch Concurrency Pipelines
  xTaskCreatePinnedToCore(logicTask, "bms_logic", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(uiTask, "display_ui", 16384, nullptr, 1, nullptr, 1);
}

void loop() { 
  vTaskDelay(portMAX_DELAY); 
}