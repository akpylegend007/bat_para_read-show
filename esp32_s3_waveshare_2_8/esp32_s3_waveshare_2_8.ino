#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// PIN CONFIG - WAVESHARE ESP32-S3 TOUCH LCD 2.8 (ST7789)
// ---------------------------------------------------------------------------
#define TFT_CS    42
#define TFT_RST   39
#define TFT_DC    41
#define TFT_MOSI  45
#define TFT_SCLK  40
#define TFT_BL    5

// Crucial: Must be set HIGH to latch battery power
#define PWR_CTRL  7

// Hardware SPI for performance on S3
SPIClass fspi(FSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&fspi, TFT_CS, TFT_DC, TFT_RST);

// ---------------------------------------------------------------------------
// BLE CONFIG - JBD BMS
// ---------------------------------------------------------------------------
static NimBLEUUID serviceUUID("ff00"); 
static NimBLEUUID rxUUID("ff02"); 
static NimBLEUUID txUUID("ff01"); 

NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pRxChar = nullptr;
NimBLERemoteCharacteristic* pTxChar = nullptr;
const NimBLEAdvertisedDevice* myDevice = nullptr;

bool doConnect = false;
bool connected = false;
bool doScan = false;
uint32_t lastRequestTime = 0;

#define MAX_PACKET_LEN 64
uint8_t packetBuffer[MAX_PACKET_LEN];
int packetLen = 0;

const uint8_t reqBasicInfo[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};

// ---------------------------------------------------------------------------
// UI CONFIG & THEME (240x320)
// ---------------------------------------------------------------------------
#define BG_BLACK     0x0000
#define PAC_YELLOW   0xFFE0
#define MAZE_BLUE    0x001F
#define PELLET_WHITE 0xFFFF
#define GHOST_RED    0xF800
#define GHOST_CYAN   0x07FF
#define BOLT_GREEN   0x07E0
#define DIM_GREY     0x4A69
#define TEXT_WHITE   0xFFFF

const int SCREEN_W = 240;
const int SCREEN_H = 320;
const int PELLET_COUNT = 14;      
const int PELLET_ROW_Y = 160;      
const int PELLET_START_X = 20;
const int PELLET_SPACING = 15;

bool mouthOpen = true;
unsigned long lastAnimMs = 0;

// Telemetry state
float packVoltage = 0.0;
float packCurrent = 0.0;
int soc = 0;
bool isCharging = false;
bool isDischarging = false;

// Function declarations
void drawAll();
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
// BLE & PACKET PARSING
// ---------------------------------------------------------------------------
void parsePacket(uint8_t* buf, size_t len) {
    if (len < 6) return;
    if (buf[0] != 0xDD || buf[1] != 0x03 || buf[2] != 0x00) return;
    
    int offset = 4;
    
    uint16_t v = (buf[offset] << 8) | buf[offset + 1];
    packVoltage = v / 100.0;
    
    int16_t c = (buf[offset + 2] << 8) | buf[offset + 3];
    packCurrent = c / 100.0;
    
    soc = buf[offset + 19];
    
    isCharging = (packCurrent > 0.05);
    isDischarging = (packCurrent < -0.05);
    
    drawAll(); // Redraw data when updated
}

void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    for (size_t i = 0; i < length; i++) {
        if (packetLen == 0 && pData[i] != 0xDD) continue;
        
        if (packetLen < MAX_PACKET_LEN) {
            packetBuffer[packetLen++] = pData[i];
        }
        
        if (packetLen >= 4 && pData[i] == 0x77) {
            uint8_t dataLength = packetBuffer[3];
            int expectedLen = 4 + dataLength + 2 + 1;
            
            if (packetLen == expectedLen) {
                parsePacket(packetBuffer, packetLen);
                packetLen = 0;
            } else if (packetLen > expectedLen) {
                packetLen = 0;
            }
        }
    }
}

class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pclient) override {
        connected = true;
        Serial.println("Connected to BMS.");
        packetLen = 0;
        drawAll();
    }
    void onDisconnect(NimBLEClient* pclient, int reason) override {
        connected = false;
        doScan = true;
        Serial.println("Disconnected from BMS. Scanning...");
        drawAll();
    }
};

bool connectToServer() {
    Serial.println("Forming a connection to BMS...");
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    if (!pClient->connect(myDevice)) {
        return false;
    }

    NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        pRemoteService = pClient->getService(NimBLEUUID("fff0"));
    }
    if (pRemoteService == nullptr) {
        pClient->disconnect();
        return false;
    }

    pRxChar = pRemoteService->getCharacteristic(rxUUID);
    pTxChar = pRemoteService->getCharacteristic(txUUID);

    if (pTxChar && pTxChar->canNotify()) {
        pTxChar->subscribe(true, notifyCallback);
        if (pRxChar) {
            pRxChar->writeValue(reqBasicInfo, sizeof(reqBasicInfo), false);
            lastRequestTime = millis();
        }
    }
    return true;
}

class MyScanCallbacks: public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        bool match = false;
        if (advertisedDevice->haveName() && advertisedDevice->getName().find("CE024AA005260138") != std::string::npos) {
            match = true;
        } else if (advertisedDevice->haveServiceUUID()) {
            if (advertisedDevice->isAdvertisingService(NimBLEUUID("ff00")) || 
                advertisedDevice->isAdvertisingService(NimBLEUUID("fff0"))) {
                match = true;
            }
        }
        if (match) {
            NimBLEDevice::getScan()->stop();
            myDevice = advertisedDevice;
            doConnect = true;
            doScan = false;
        }
    }
};

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    
    // Crucial Power Latch for Waveshare Board
    pinMode(PWR_CTRL, OUTPUT);
    digitalWrite(PWR_CTRL, HIGH);
    
    // Init Backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); // Turn on fully
    
    // Init Display via FSPI
    fspi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 320); 
    tft.setRotation(0); // Portrait
    tft.fillScreen(BG_BLACK);
    
    drawStaticFrame();
    drawAll();

    NimBLEDevice::init("");
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(new MyScanCallbacks());
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); 
    pBLEScan->setActiveScan(true); 
    doScan = true;
}

void loop() {
    unsigned long now = millis();
    
    // Pacman mouth chomp animation (~4 frames/sec)
    if (now - lastAnimMs > 250) {
        lastAnimMs = now;
        mouthOpen = !mouthOpen;
        drawTitleBar(); 
    }

    if (doConnect) {
        if (!connectToServer()) {
            doScan = true;
        }
        doConnect = false;
    }

    if (connected) {
        // Query BMS every 2 seconds
        if (now - lastRequestTime > 2000) {
            if (pRxChar) {
                pRxChar->writeValue(reqBasicInfo, sizeof(reqBasicInfo), false);
            }
            lastRequestTime = now;
        }
    } else if (doScan) {
        NimBLEDevice::getScan()->start(5, false);
        doScan = true;
    }
    
    delay(10);
}

// ---------------------------------------------------------------------------
// UI DRAWING FUNCTIONS
// ---------------------------------------------------------------------------
void drawStaticFrame() {
    // Top & bottom maze-style border bars
    tft.fillRect(0, 0, SCREEN_W, 5, MAZE_BLUE);
    tft.fillRect(0, SCREEN_H - 5, SCREEN_W, 5, MAZE_BLUE);
    for (int x = 6; x < SCREEN_W; x += 16) {
        tft.fillRect(x, 0, 6, 5, BG_BLACK);
        tft.fillRect(x, SCREEN_H - 5, 6, 5, BG_BLACK);
    }
}

void drawAll() {
    drawTitleBar();
    
    if (!connected) {
        // Clear main area and show connecting text
        tft.fillRect(0, 35, SCREEN_W, 250, BG_BLACK);
        tft.setTextColor(DIM_GREY);
        tft.setTextSize(2);
        tft.setCursor(45, 140);
        tft.print("SCANNING...");
        return;
    }
    
    drawSocNumber();
    drawPelletBar();
    drawStateBadge();
    
    // Redraw volt/curr headers
    tft.setTextColor(DIM_GREY);
    tft.setTextSize(2);
    tft.setCursor(20, 240);
    tft.print("VOLT");
    tft.setCursor(140, 240);
    tft.print("CURR");
    
    drawVoltageCurrent();
}

void drawTitleBar() {
    tft.fillRect(0, 10, SCREEN_W, 20, BG_BLACK);
    tft.setTextColor(PAC_YELLOW);
    tft.setTextSize(2); // 12x16 pixels per char
    tft.setCursor(50, 12);
    tft.print("PAC-BATTERY");
    drawPacmanIcon(25, 20, 8, mouthOpen);
}

void drawSocNumber() {
    tft.fillRect(0, 50, SCREEN_W, 60, BG_BLACK);
    
    uint16_t color = PAC_YELLOW;
    if (soc <= 15) color = GHOST_RED;
    else if (soc <= 35) color = GHOST_RED;
    else if (isCharging) color = BOLT_GREEN;

    tft.setTextColor(color);
    tft.setTextSize(7); // HUGE SIZE: ~42x56 per character

    char buf[6];
    sprintf(buf, "%d%%", soc);
    int textW = strlen(buf) * 42;
    int x = (SCREEN_W - textW) / 2;
    tft.setCursor(x, 50);
    tft.print(buf);
}

void drawPelletBar() {
    tft.fillRect(0, PELLET_ROW_Y - 12, SCREEN_W, 28, BG_BLACK);

    int filledPellets = (soc * PELLET_COUNT) / 100;
    int eatenPellets = PELLET_COUNT - filledPellets;

    for (int i = 0; i < PELLET_COUNT; i++) {
        int px = PELLET_START_X + i * PELLET_SPACING;
        if (i < eatenPellets) continue;
        tft.fillCircle(px, PELLET_ROW_Y, 3, PELLET_WHITE);
    }

    int pacX = PELLET_START_X + eatenPellets * PELLET_SPACING - 12;
    if (pacX < 12) pacX = 12;
    drawPacmanIcon(pacX, PELLET_ROW_Y, 10, mouthOpen);

    if (eatenPellets > 1) {
        uint16_t ghostColor = isCharging ? GHOST_CYAN : GHOST_RED; 
        drawGhostIcon(PELLET_START_X - 10, PELLET_ROW_Y, 9, ghostColor);
    }
}

void drawStateBadge() {
    tft.fillRect(0, 195, SCREEN_W, 30, BG_BLACK);
    tft.setTextSize(3); // 18x24 px per char
    
    if (isCharging) {
        tft.setTextColor(BOLT_GREEN);
        tft.setCursor(45, 195);
        tft.print("CHARGING");
        drawBolt(20, 198, BOLT_GREEN, 2);
    } else if (isDischarging) {
        tft.setTextColor(GHOST_RED); 
        tft.setCursor(45, 195);
        tft.print("DRAINING");
        drawGhostIcon(25, 207, 9, GHOST_RED);
    } else {
        tft.setTextColor(TEXT_WHITE);
        tft.setCursor(85, 195);
        tft.print("IDLE");
    }
}

void drawVoltageCurrent() {
    tft.fillRect(0, 265, SCREEN_W, 30, BG_BLACK);

    char vbuf[8], cbuf[8];
    dtostrf(packVoltage, 4, 1, vbuf);
    dtostrf(fabs(packCurrent), 4, 1, cbuf);

    tft.setTextColor(TEXT_WHITE);
    tft.setTextSize(3);
    tft.setCursor(10, 265);
    tft.print(vbuf);
    
    tft.setTextColor(isCharging ? BOLT_GREEN : GHOST_RED); 
    tft.setCursor(130, 265);
    tft.print(isCharging ? "+" : "-");
    tft.print(cbuf);
}

// ---------------------------------------------------------------------------
// SHAPE DRAWING PRIMITIVES
// ---------------------------------------------------------------------------
void drawPacmanIcon(int cx, int cy, int r, bool open) {
    tft.fillCircle(cx, cy, r, PAC_YELLOW);
    if (open) {
        tft.fillTriangle(cx, cy,
                          cx + r + 2, cy - r / 2 - 2,
                          cx + r + 2, cy + r / 2 + 2,
                          BG_BLACK);
    }
    tft.fillCircle(cx - r / 3, cy - r / 2, max(2, r / 5), BG_BLACK);
}

void drawGhostIcon(int cx, int cy, int r, uint16_t color) {
    tft.fillCircle(cx, cy - r / 3, r, color);
    tft.fillRect(cx - r, cy - r / 3, r * 2, r, color);
    int footW = (r * 2) / 3;
    for (int i = 0; i < 3; i++) {
        int fx = cx - r + i * footW;
        tft.fillTriangle(fx, cy + r / 2,
                          fx + footW / 2, cy + r,
                          fx + footW, cy + r / 2,
                          color);
    }
    tft.fillCircle(cx - r / 3, cy - r / 3, max(2, r / 4), TEXT_WHITE);
    tft.fillCircle(cx + r / 3, cy - r / 3, max(2, r / 4), TEXT_WHITE);
    tft.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 6), BG_BLACK);
    tft.fillCircle(cx + r / 3, cy - r / 3, max(1, r / 6), BG_BLACK);
}

void drawBolt(int x, int y, uint16_t color, int scale) {
    // Scaled version of lightning bolt
    tft.fillTriangle(x + 4*scale, y, x, y + 5*scale, x + 3*scale, y + 5*scale, color);
    tft.fillTriangle(x + 3*scale, y + 5*scale, x + 7*scale, y + 5*scale, x + 2*scale, y + 10*scale, color);
}
