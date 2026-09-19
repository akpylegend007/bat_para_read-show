/*
  ============================================================================
   PAC-BATTERY :: Pacman-themed Battery Status UI
   Display: 1.44" 128x128 SPI TFT, ST7735 controller
   (robu.in "1.44 Inch 128x128 ST7735 Controller SPI Serial Port TFT Display")
   Target: ESP32 (any dev board - DevKitC, WROOM32, etc.)
  ============================================================================

  WHAT THIS SHOWS (single page, 128x128):
    - Title bar with a little chomping Pacman
    - Big SOC% number
    - A row of "power pellets" that Pacman eats, filling with charge level
    - A state badge: CHARGING (bolt, green) / DISCHARGING (ghost, orange)
    - Voltage + current readout at the bottom
    - Blue maze-style border top & bottom, classic arcade palette

  WIRING (8-pin module: GND, VCC, SCL, SDA, RES, DC, CS, BLK)
  ESP32 logic is native 3.3V, same as this display, so it can be wired
  directly with no level shifter.

    Module Pin  ->  Meaning        ->  ESP32 GPIO (default in this sketch)
    GND         ->  Ground         ->  GND
    VCC         ->  Power (3.3V)   ->  3V3   (do NOT use 5V/VIN)
    SCL         ->  SPI Clock      ->  GPIO18  (VSPI SCK, hardware SPI)
    SDA         ->  SPI MOSI       ->  GPIO23  (VSPI MOSI, hardware SPI)
    RES         ->  Reset          ->  GPIO4
    DC          ->  Data/Command   ->  GPIO2
    CS          ->  Chip Select    ->  GPIO5
    BLK         ->  Backlight      ->  3V3 directly, or GPIO15 (PWM dimming)

  All pins below except SCL/SDA are freely reassignable to any free GPIO -
  just update the #defines. SCL/SDA use the ESP32's hardware VSPI pins for
  speed; if you need different SPI pins, initialize SPI with SPI.begin()
  and pass custom pins before tft.initR().

  LIBRARIES (Arduino IDE -> Library Manager, install for "esp32" board):
    - "Adafruit GFX Library"
    - "Adafruit ST7735 and ST7789 Library"
  BOARD: Tools -> Board -> ESP32 Arduino -> (your board, e.g. "ESP32 Dev Module")

  This file runs a DEMO simulation of battery charge/discharge out of the box
  so you can see the UI immediately. Search for "REAL SENSOR HOOK" below to
  wire it up to a real fuel gauge (MAX17048 over I2C), an INA219, or a plain
  voltage divider read on an ADC pin (e.g. GPIO34, ADC1-only, input-only).
  ============================================================================
*/

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ---------------------------------------------------------------------------
// PIN CONFIG - edit to match your wiring
// ---------------------------------------------------------------------------
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2
// SCL -> GPIO18 (SCK), SDA -> GPIO23 (MOSI): ESP32 hardware VSPI pins,
// used automatically by the library/SPI class, no #define needed.

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ---------------------------------------------------------------------------
// CLASSIC ARCADE PALETTE (RGB565)
// ---------------------------------------------------------------------------
#define BG_BLACK     0x0000
#define PAC_YELLOW   0xFFE0
#define MAZE_BLUE    0x001F
#define PELLET_WHITE 0xFFFF
#define GHOST_RED    0xF800
#define GHOST_ORANGE 0xFD20
#define GHOST_CYAN   0x07FF
#define GHOST_PINK   0xFB56
#define BOLT_GREEN   0x07E0
#define DIM_GREY     0x4A69
#define TEXT_WHITE   0xFFFF

// ---------------------------------------------------------------------------
// BATTERY STATE (this is what a real sensor would fill in)
// ---------------------------------------------------------------------------
struct BatteryState {
  int   soc;        // 0-100 %
  float voltage;     // volts
  float current;     // amps (positive = charging, negative = discharging)
  bool  isCharging;
};

BatteryState batt = { 72, 3.87, -0.42, false };

// Layout constants
const int SCREEN_W = 128;
const int SCREEN_H = 128;
const int PELLET_COUNT = 10;      // one pellet per 10%
const int PELLET_ROW_Y = 66;
const int PELLET_START_X = 12;
const int PELLET_SPACING = 10;

// Animation state
bool mouthOpen = true;
unsigned long lastAnimMs = 0;
unsigned long lastDemoMs = 0;
int demoDir = 1; // -1 discharging, +1 charging (demo only)

void drawStaticFrame();
void drawTitleBar();
void drawSocNumber();
void drawPelletBar();
void drawStateBadge();
void drawVoltageCurrent();
void drawPacmanIcon(int cx, int cy, int r, bool open);
void drawGhostIcon(int cx, int cy, int r, uint16_t color);
void drawBolt(int x, int y, uint16_t color);

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  
  pinMode(TFT_BLK, OUTPUT);
  analogWrite(TFT_BLK, 128); // 50% brightness to reduce heat

  tft.initR(INITR_144GREENTAB);   // use INITR_144GREENTAB for most 1.44" boards
  tft.setRotation(0);
  tft.fillScreen(BG_BLACK);

  drawStaticFrame();
  drawAll();
}

void loop() {
  unsigned long now = millis();

  // ---- Pacman mouth chomp animation, ~4 frames/sec ----
  if (now - lastAnimMs > 250) {
    lastAnimMs = now;
    mouthOpen = !mouthOpen;
    drawTitleBar(); // only redraw the small area that animates
  }

  // ---- DEMO simulation: replace this block with REAL SENSOR HOOK ----
  if (now - lastDemoMs > 700) {
    lastDemoMs = now;
    batt.soc += demoDir;
    if (batt.soc <= 5)  { demoDir = 1;  batt.isCharging = true;  }
    if (batt.soc >= 100) { demoDir = -1; batt.isCharging = false; }
    batt.voltage = 3.30 + (batt.soc / 100.0) * 0.90;      // 3.30V-4.20V curve
    batt.current = batt.isCharging ? 0.35 + (rand() % 20) / 100.0
                                    : -(0.20 + (rand() % 40) / 100.0);
    drawAll();
  }

  /* ------------------------- REAL SENSOR HOOK -----------------------------
     Example using a MAX17048 fuel gauge (Adafruit_MAX1704X library):

       batt.soc        = maxlipo.cellPercent();
       batt.voltage    = maxlipo.cellVoltage();
       batt.isCharging = (chargeCurrentReadingIsPositive);
       batt.current    = readCurrentFromYourShuntOrINA219();
       drawAll();

     Or a simple analog voltage divider:
       float v = analogRead(A0) * (3.3 / 4095.0) * dividerRatio;
       batt.voltage = v;
       batt.soc = mapVoltageToPercent(v); // your own LUT/curve
       drawAll();
  ---------------------------------------------------------------------------*/
}

// ---------------------------------------------------------------------------
// STATIC ELEMENTS drawn once: maze borders + labels
// ---------------------------------------------------------------------------
void drawStaticFrame() {
  // Top & bottom maze-style border bars
  tft.fillRect(0, 0, SCREEN_W, 3, MAZE_BLUE);
  tft.fillRect(0, SCREEN_H - 3, SCREEN_W, 3, MAZE_BLUE);
  for (int x = 4; x < SCREEN_W; x += 12) {
    tft.fillRect(x, 0, 4, 3, BG_BLACK); // dashed effect
    tft.fillRect(x, SCREEN_H - 3, 4, 3, BG_BLACK);
  }

  tft.setTextColor(DIM_GREY);
  tft.setTextSize(1);
  tft.setCursor(4, 88);
  tft.print("VOLT");
  tft.setCursor(74, 88);
  tft.print("CURR");
}

// ---------------------------------------------------------------------------
// TOP-LEVEL REDRAW
// ---------------------------------------------------------------------------
void drawAll() {
  drawTitleBar();
  drawSocNumber();
  drawPelletBar();
  drawStateBadge();
  drawVoltageCurrent();
}

// ---------------------------------------------------------------------------
// Title bar: "BATTERY" label + a tiny chomping Pacman that runs in place
// ---------------------------------------------------------------------------
void drawTitleBar() {
  static bool first = true;
  if (first) {
    tft.fillRect(0, 6, SCREEN_W, 14, BG_BLACK);
    tft.setTextColor(PAC_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(24, 9);
    tft.print("PAC-BATTERY");
    first = false;
  } else {
    // Only clear the icon area to stop text flickering
    tft.fillRect(0, 6, 20, 14, BG_BLACK);
  }

  drawPacmanIcon(8, 13, 5, mouthOpen);
}

// ---------------------------------------------------------------------------
// Big SOC percentage number, color-coded by level
// ---------------------------------------------------------------------------
void drawSocNumber() {
  uint16_t color = PAC_YELLOW;
  if (batt.soc <= 15) color = GHOST_RED;
  else if (batt.soc <= 35) color = GHOST_ORANGE;
  else if (batt.isCharging) color = BOLT_GREEN;

  tft.setTextColor(color, BG_BLACK);
  tft.setTextSize(3);

  char buf[10];
  sprintf(buf, "%d%%  ", batt.soc); // Pad spaces to overwrite old
  int textW = 18 * 4; // Approx fixed max width for centering
  int x = (SCREEN_W - textW) / 2 + 12;
  tft.setCursor(x, 28);
  tft.print(buf);
}

// ---------------------------------------------------------------------------
// Pellet bar
// ---------------------------------------------------------------------------
void drawPelletBar() {
  tft.fillRect(0, PELLET_ROW_Y - 6, SCREEN_W, 14, BG_BLACK);

  int filledPellets = (batt.soc * PELLET_COUNT) / 100; // pellets remaining ahead
  int eatenPellets = PELLET_COUNT - filledPellets;

  for (int i = 0; i < PELLET_COUNT; i++) {
    int px = PELLET_START_X + i * PELLET_SPACING;
    if (i < eatenPellets) continue;
    tft.fillCircle(px, PELLET_ROW_Y, 2, PELLET_WHITE);
  }

  int pacX = PELLET_START_X + eatenPellets * PELLET_SPACING - 6;
  if (pacX < 6) pacX = 6;
  drawPacmanIcon(pacX, PELLET_ROW_Y, 6, mouthOpen);

  if (eatenPellets > 1) {
    uint16_t ghostColor = batt.isCharging ? GHOST_CYAN : GHOST_RED;
    drawGhostIcon(PELLET_START_X - 6, PELLET_ROW_Y, 5, ghostColor);
  }
}

// ---------------------------------------------------------------------------
// State badge
// ---------------------------------------------------------------------------
void drawStateBadge() {
  tft.fillRect(0, 74, 20, 12, BG_BLACK); // Clear icon area

  tft.setTextSize(1);
  if (batt.isCharging) {
    tft.setTextColor(BOLT_GREEN, BG_BLACK);
    tft.setCursor(30, 76);
    tft.print("CHARGING   ");
    drawBolt(18, 75, BOLT_GREEN);
  } else {
    tft.setTextColor(GHOST_ORANGE, BG_BLACK);
    tft.setCursor(22, 76);
    tft.print("DISCHARGING");
    drawGhostIcon(10, 80, 4, GHOST_ORANGE);
  }
}

// ---------------------------------------------------------------------------
// Voltage / current numeric readout
// ---------------------------------------------------------------------------
void drawVoltageCurrent() {
  char vbuf[10], cbuf[10];
  dtostrf(batt.voltage, 4, 2, vbuf);
  dtostrf(fabs(batt.current), 4, 2, cbuf);

  tft.setTextColor(TEXT_WHITE, BG_BLACK);
  tft.setTextSize(1);
  tft.setCursor(4, 98);
  tft.print(vbuf);
  tft.print("V ");

  tft.setTextColor(batt.isCharging ? BOLT_GREEN : GHOST_ORANGE, BG_BLACK);
  tft.setCursor(74, 98);
  tft.print(batt.isCharging ? "+" : "-");
  tft.print(cbuf);
  tft.print("A ");
}

// ---------------------------------------------------------------------------
// PRIMITIVE SPRITES
// ---------------------------------------------------------------------------

// Simple chomping Pacman: yellow circle with a black wedge mouth on the right
void drawPacmanIcon(int cx, int cy, int r, bool open) {
  tft.fillCircle(cx, cy, r, PAC_YELLOW);
  if (open) {
    // mouth wedge pointing right, ~50 degrees total
    tft.fillTriangle(cx, cy,
                      cx + r + 1, cy - r / 2 - 1,
                      cx + r + 1, cy + r / 2 + 1,
                      BG_BLACK);
  }
  // eye
  tft.fillCircle(cx - r / 3, cy - r / 2, max(1, r / 5), BG_BLACK);
}

// Simple ghost: rounded body + wavy bottom (approximated with 3 feet) + eyes
void drawGhostIcon(int cx, int cy, int r, uint16_t color) {
  tft.fillCircle(cx, cy - r / 3, r, color);
  tft.fillRect(cx - r, cy - r / 3, r * 2, r, color);
  // wavy feet (3 little triangles)
  int footW = (r * 2) / 3;
  for (int i = 0; i < 3; i++) {
    int fx = cx - r + i * footW;
    tft.fillTriangle(fx, cy + r / 2,
                      fx + footW / 2, cy + r,
                      fx + footW, cy + r / 2,
                      color);
  }
  // eyes
  tft.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 4), TEXT_WHITE);
  tft.fillCircle(cx + r / 3, cy - r / 3, max(1, r / 4), TEXT_WHITE);
  tft.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 6), BG_BLACK);
  tft.fillCircle(cx + r / 3, cy - r / 3, max(1, r / 6), BG_BLACK);
}

// Small lightning bolt for the CHARGING badge
void drawBolt(int x, int y, uint16_t color) {
  tft.fillTriangle(x + 4, y, x, y + 5, x + 3, y + 5, color);
  tft.fillTriangle(x + 3, y + 5, x + 7, y + 5, x + 2, y + 10, color);
}
