#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// PIN CONFIG - ST7735
// ---------------------------------------------------------------------------
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2
#define TFT_BLK   3 // Backlight PWM pin
// SCL -> GPIO18, SDA -> GPIO23 (Hardware VSPI)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

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
// UI CONFIG
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

const int SCREEN_W = 128;
const int SCREEN_H = 128;
const int PELLET_COUNT = 10;      
const int PELLET_ROW_Y = 56;      
const int PELLET_START_X = 12;
const int PELLET_SPACING = 10;

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
    
    // Setup TFT Backlight PWM to reduce brightness & heat
    pinMode(TFT_BLK, OUTPUT);
    analogWrite(TFT_BLK, 128); // 50% brightness (0-255)

    tft.initR(INITR_144GREENTAB);
    tft.setRotation(0);
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
    tft.fillRect(0, 0, SCREEN_W, 3, MAZE_BLUE);
    tft.fillRect(0, SCREEN_H - 3, SCREEN_W, 3, MAZE_BLUE);
    for (int x = 4; x < SCREEN_W; x += 12) {
        tft.fillRect(x, 0, 4, 3, BG_BLACK);
        tft.fillRect(x, SCREEN_H - 3, 4, 3, BG_BLACK);
    }
}

void drawAll() {
    drawTitleBar();
    
    if (!connected) {
        // Clear main area and show connecting text
        tft.fillRect(0, 18, SCREEN_W, 100, BG_BLACK);
        tft.setTextColor(DIM_GREY);
        tft.setTextSize(1);
        tft.setCursor(30, 60);
        tft.print("SCANNING...");
        return;
    }
    
    drawSocNumber();
    drawPelletBar();
    drawStateBadge();
    
    // Redraw volt/curr headers since they were cleared when disconnected
    tft.setTextColor(DIM_GREY);
    tft.setTextSize(1);
    tft.setCursor(8, 94);
    tft.print("VOLT");
    tft.setCursor(76, 94);
    tft.print("CURR");
    
    drawVoltageCurrent();
}

void drawTitleBar() {
    static bool first = true;
    if (first) {
        tft.fillRect(0, 6, SCREEN_W, 10, BG_BLACK);
        tft.setTextColor(PAC_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(24, 7);
        tft.print("PAC-BATTERY");
        first = false;
    } else {
        // Just clear the Pacman area to prevent full-bar flickering
        tft.fillRect(0, 6, 20, 10, BG_BLACK);
    }
    drawPacmanIcon(8, 11, 4, mouthOpen);
}

void drawSocNumber() {
    uint16_t color = PAC_YELLOW;
    if (soc <= 15) color = GHOST_RED;
    else if (soc <= 35) color = GHOST_RED; 
    else if (isCharging) color = BOLT_GREEN;

    tft.setTextColor(color, BG_BLACK);
    tft.setTextSize(4);

    char buf[10];
    sprintf(buf, "%d%%  ", soc); // pad with spaces to overwrite old digits
    int textW = 24 * 4; // Max width approx for "100%"
    int x = (SCREEN_W - textW) / 2 + 12;
    tft.setCursor(x, 20);
    tft.print(buf);
}

void drawPelletBar() {
    // Only clear the pellet area, not the whole screen
    tft.fillRect(0, PELLET_ROW_Y - 6, SCREEN_W, 14, BG_BLACK);

    int filledPellets = (soc * PELLET_COUNT) / 100;
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
        uint16_t ghostColor = isCharging ? GHOST_CYAN : GHOST_RED;
        drawGhostIcon(PELLET_START_X - 6, PELLET_ROW_Y, 5, ghostColor);
    }
}

void drawStateBadge() {
    // We must clear just the icon area, text will self-clear
    tft.fillRect(0, 72, 20, 16, BG_BLACK);
    tft.setTextSize(2); 
    
    if (isCharging) {
        tft.setTextColor(BOLT_GREEN, BG_BLACK);
        tft.setCursor(20, 72);
        tft.print("CHARGING  ");
        drawBolt(6, 73, BOLT_GREEN);
    } else if (isDischarging) {
        tft.setTextColor(GHOST_RED, BG_BLACK); 
        tft.setCursor(20, 72);
        tft.print("DRAINING  ");
        drawGhostIcon(8, 79, 6, GHOST_RED);
    } else {
        tft.setTextColor(TEXT_WHITE, BG_BLACK);
        tft.setCursor(20, 72);
        tft.print("IDLE      ");
    }
}

void drawVoltageCurrent() {
    char vbuf[10], cbuf[10];
    sprintf(vbuf, "%4.1fV ", packVoltage);
    sprintf(cbuf, "%c%4.1fA ", isCharging ? '+' : '-', fabs(packCurrent));

    tft.setTextColor(TEXT_WHITE, BG_BLACK);
    tft.setTextSize(2); 
    tft.setCursor(2, 106);
    tft.print(vbuf);
    
    tft.setTextColor(isCharging ? BOLT_GREEN : GHOST_RED, BG_BLACK); 
    tft.setCursor(68, 106);
    tft.print(cbuf);
}

void drawPacmanIcon(int cx, int cy, int r, bool open) {
    tft.fillCircle(cx, cy, r, PAC_YELLOW);
    if (open) {
        tft.fillTriangle(cx, cy,
                          cx + r + 1, cy - r / 2 - 1,
                          cx + r + 1, cy + r / 2 + 1,
                          BG_BLACK);
    }
    tft.fillCircle(cx - r / 3, cy - r / 2, max(1, r / 5), BG_BLACK);
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
    tft.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 4), TEXT_WHITE);
    tft.fillCircle(cx + r / 3, cy - r / 3, max(1, r / 4), TEXT_WHITE);
    tft.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 6), BG_BLACK);
    tft.fillCircle(cx + r / 3, cy - r / 3, max(1, r / 6), BG_BLACK);
}

void drawBolt(int x, int y, uint16_t color) {
    tft.fillTriangle(x + 4, y, x, y + 5, x + 3, y + 5, color);
    tft.fillTriangle(x + 3, y + 5, x + 7, y + 5, x + 2, y + 10, color);
}
