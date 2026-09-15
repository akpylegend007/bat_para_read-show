#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBLEDevice.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define I2C_ADDRESS   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Standard JBD BMS BLE UUIDs
static NimBLEUUID serviceUUID("ff00"); 
static NimBLEUUID rxUUID("ff02"); // Write characteristic
static NimBLEUUID txUUID("ff01"); // Notify characteristic

NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pRxChar = nullptr;
NimBLERemoteCharacteristic* pTxChar = nullptr;
const NimBLEAdvertisedDevice* myDevice = nullptr;

bool doConnect = false;
bool connected = false;
bool doScan = false;
uint32_t lastRequestTime = 0;

// Telemetry data
float packVoltage = 0.0;
float packCurrent = 0.0;
float capacityAh = 0.0;
float designCapAh = 0.0;
uint16_t cycles = 0;
uint16_t protectStatus = 0;
int soc = 0;
float maxTemp = 0.0;
String opMode = "Idle";

uint8_t displayPage = 0;
unsigned long lastPageChange = 0;
#define NUM_PAGES 3

// Packet assembler buffer
#define MAX_PACKET_LEN 64
uint8_t packetBuffer[MAX_PACKET_LEN];
int packetLen = 0;

// JBD Basic Info Request payload (0x03)
const uint8_t reqBasicInfo[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};

void updateDisplay() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (!connected) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Scanning/Connecting...");
        display.print("BMS Not Found");
        display.display();
        return;
    }

    if (displayPage == 0) {
        // Page 0: Main Dash
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.print("CLEAN ELEC");
        display.setCursor(90, 0);
        display.print("SOC:");
        display.print(soc);
        display.print("%");
        
        display.drawLine(0, 9, 128, 9, SSD1306_WHITE); 
        
        display.setCursor(0, 14);
        display.setTextSize(2);
        display.print(packVoltage, 1);
        display.print("V ");
        if (packCurrent > 0) display.print("+");
        display.print((int)packCurrent);
        display.print("A");
    } else if (displayPage == 1) {
        // Page 1: Capacity & Thermals
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.print("--- SYSTEM HEALTH ---");
        
        display.setCursor(0, 12);
        display.print("Cap: ");
        display.print(capacityAh, 1);
        display.print("/");
        display.print(designCapAh, 1);
        display.print("Ah");
        
        display.setCursor(0, 22);
        display.print("Cyc: ");
        display.print(cycles);
        display.print("  T: ");
        display.print((int)maxTemp);
        display.print("C");
    } else if (displayPage == 2) {
        // Page 2: Status & Alarms
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.print("--- DIAGNOSTICS ---");
        
        display.setCursor(0, 12);
        display.print("Mode: ");
        display.print(opMode);
        
        display.setCursor(0, 22);
        display.print("Alarms: ");
        if (protectStatus == 0) {
            display.print("NONE (OK)");
        } else {
            display.print("CODE ");
            display.print(protectStatus, HEX);
        }
    }
    
    display.display();
}

void parsePacket(uint8_t* buf, size_t len) {
    if (len < 6) return;
    if (buf[0] != 0xDD || buf[1] != 0x03 || buf[2] != 0x00) return;
    
    int offset = 4;
    
    uint16_t v = (buf[offset] << 8) | buf[offset + 1];
    packVoltage = v / 100.0;
    
    int16_t c = (buf[offset + 2] << 8) | buf[offset + 3];
    packCurrent = c / 100.0;
    
    uint16_t cap = (buf[offset + 4] << 8) | buf[offset + 5];
    capacityAh = cap * 10.0 / 1000.0;
    
    uint16_t dcap = (buf[offset + 6] << 8) | buf[offset + 7];
    designCapAh = dcap * 10.0 / 1000.0;
    
    cycles = (buf[offset + 8] << 8) | buf[offset + 9];
    protectStatus = (buf[offset + 16] << 8) | buf[offset + 17];
    soc = buf[offset + 19];
    
    uint8_t numTemps = buf[offset + 22];
    if (numTemps > 0 && len >= (offset + 23 + numTemps * 2)) {
        maxTemp = -100.0;
        for (int j = 0; j < numTemps; j++) {
            int base = offset + 23 + j * 2;
            uint16_t tRaw = (buf[base] << 8) | buf[base + 1];
            float tC = (tRaw - 2731) / 10.0;
            if (tC > maxTemp) maxTemp = tC;
        }
    }
    
    if (packCurrent > 0.05) opMode = "Charging";
    else if (packCurrent < -0.05) opMode = "Discharging";
    else opMode = "Idle";
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
                updateDisplay();
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
        packetLen = 0; // reset buffer on connection
        updateDisplay();
    }
    void onDisconnect(NimBLEClient* pclient, int reason) override {
        connected = false;
        doScan = true;
        Serial.println("Disconnected from BMS. Scanning...");
        updateDisplay();
    }
};

bool connectToServer() {
    Serial.println("Forming a connection to BMS...");
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    if (!pClient->connect(myDevice)) {
        Serial.println("Failed to connect.");
        return false;
    }

    Serial.println("Connected! Enumerating services...");
    
    for (auto pService : pClient->getServices(true)) {
        Serial.print("Service: ");
        Serial.println(pService->getUUID().toString().c_str());
        
        for (auto pChar : pService->getCharacteristics(true)) {
            Serial.print(" - Char: ");
            Serial.print(pChar->getUUID().toString().c_str());
            if (pChar->canRead()) Serial.print(" [Read]");
            if (pChar->canWrite()) Serial.print(" [Write]");
            if (pChar->canNotify()) Serial.print(" [Notify]");
            Serial.println();
        }
    }

    NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        pRemoteService = pClient->getService(NimBLEUUID("fff0"));
    }
    if (pRemoteService == nullptr) {
        Serial.println("Target service not found.");
        pClient->disconnect();
        return false;
    }

    pRxChar = pRemoteService->getCharacteristic(rxUUID);
    pTxChar = pRemoteService->getCharacteristic(txUUID);

    if (pTxChar && pTxChar->canNotify()) {
        pTxChar->subscribe(true, notifyCallback);
        Serial.println("Subscribed to TX notifications.");
        
        if (pRxChar) {
            pRxChar->writeValue(reqBasicInfo, sizeof(reqBasicInfo), false);
            lastRequestTime = millis();
        }
    } else {
        Serial.println("Failed to subscribe to TX notifications.");
    }
    
    return true;
}

class MyScanCallbacks: public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        Serial.print("Discovered: ");
        Serial.println(advertisedDevice->toString().c_str());
        
        bool match = false;
        
        // Match Clean Electric LFPGo400 specifically, or fallback to standard services
        if (advertisedDevice->haveName() && advertisedDevice->getName().find("CE024AA005260138") != std::string::npos) {
            match = true;
        } else if (advertisedDevice->haveServiceUUID()) {
            if (advertisedDevice->isAdvertisingService(NimBLEUUID("ff00")) || 
                advertisedDevice->isAdvertisingService(NimBLEUUID("fff0"))) {
                match = true;
            }
        }

        if (match) {
            Serial.print("Found Target BMS: ");
            Serial.println(advertisedDevice->toString().c_str());
            
            NimBLEDevice::getScan()->stop();
            myDevice = advertisedDevice;
            doConnect = true;
            doScan = false;
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Starting LFPGo400 Dashboard...");
    
    Wire.begin(); 
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Halt
    }
    
    updateDisplay();

    NimBLEDevice::init("");
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(new MyScanCallbacks());
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); 
    pBLEScan->setActiveScan(true); // Active scan required to get Scan Response names
    
    doScan = true;
}

void loop() {
    if (doConnect) {
        if (!connectToServer()) {
            doScan = true;
        }
        doConnect = false;
    }

    if (connected) {
        // Cyclic Polling Loop: Every 2 seconds query the 0x03 characteristic
        if (millis() - lastRequestTime > 2000) {
            if (pRxChar) {
                pRxChar->writeValue(reqBasicInfo, sizeof(reqBasicInfo), false);
            }
            lastRequestTime = millis();
        }
        
        // Cycle pages every 3.5 seconds
        if (millis() - lastPageChange > 3500) {
            displayPage = (displayPage + 1) % NUM_PAGES;
            lastPageChange = millis();
            updateDisplay();
        }
    } else if (doScan) {
        NimBLEDevice::getScan()->start(5, false); // Scan for 5 seconds
        doScan = true; // Loop scan
    }
    
    delay(100);
}
