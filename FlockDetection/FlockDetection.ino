#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <TinyGPS++.h>
#include <RTClib.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// Hardware Config
#define BUZZER_PIN A3
#define SD_CS_PIN  D2
#define BUTTON_PIN D1
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// GPS Config (XIAO ESP32-S3 Serial1)
#define GPS_SERIAL Serial1 

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);
RTC_PCF8563 rtc;
TinyGPSPlus gps;

// Scanning Config
#define MAX_CHANNEL 13
#define CHANNEL_HOP_INTERVAL 500
#define BLE_SCAN_DURATION 1
#define BLE_SCAN_INTERVAL 5000
#define IGNORE_WEAK_RSSI -80  

// Global Variables
static uint8_t current_channel = 1;
static unsigned long last_channel_hop = 0;
static unsigned long last_ble_scan = 0;
static bool triggered = false;
static NimBLEScan* pBLEScan;
bool sd_available = false;

// UI & Stats
int current_screen = 0; 
unsigned long last_button_press = 0;
long session_wifi = 0, session_ble = 0;
long lifetime_wifi = 0, lifetime_ble = 0;
unsigned long lifetime_seconds = 0;
unsigned long session_start_time = 0;

// Log Buffers
String live_logs[5] = {"", "", "", "", ""};
String last_cap_type = "None";
String last_cap_mac = "--:--:--:--:--:--";
int last_cap_rssi = 0;
String last_cap_time = "00:00:00";

// Timers
unsigned long last_uptime_update = 0;
unsigned long last_anim_update = 0;
unsigned long last_stats_update = 0;
unsigned long last_time_save = 0;
int scan_line_x = 0;

// UI Bitmaps
const unsigned char wifi_icon[] PROGMEM = { 0x3C, 0x42, 0x81, 0x24, 0x18, 0x00, 0x18, 0x00 };
const unsigned char bt_icon[] PROGMEM =   { 0x10, 0x38, 0x54, 0x10, 0x10, 0x54, 0x38, 0x10 };
const unsigned char clock_icon[] PROGMEM ={ 0x3C, 0x42, 0x42, 0x52, 0x4A, 0x42, 0x3C, 0x00 };

// Detection Patterns
static const char* wifi_ssid_patterns[] = { "flock", "Flock", "FLOCK", "FS Ext Battery", "Penguin", "Pigvision" };
static const char* mac_prefixes[] = { 
    "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84", "f0:82:c0", "1c:34:f1", "38:5b:44"
};

// ============================================================================
// HELPERS
// ============================================================================

void beep(int freq, int dur) {
    tone(BUZZER_PIN, freq, dur);
    delay(dur + 50);
}

String format_time(unsigned long total_sec) {
    unsigned long h = total_sec / 3600;
    unsigned long m = (total_sec / 60) % 60;
    unsigned long s = total_sec % 60;
    char buf[10];
    sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
    return String(buf);
}

void log_detection(String type, int rssi, String mac) {
    if (type.indexOf("WIFI") > 0) session_wifi++; else session_ble++;
    last_cap_type = type; last_cap_mac = mac; last_cap_rssi = rssi;
    last_cap_time = format_time((millis() - session_start_time)/1000);

    // Update Live Log
    for (int i = 4; i > 0; i--) live_logs[i] = live_logs[i-1];
    live_logs[0] = "[!] " + type + " (" + String(rssi) + ")";

    if (sd_available) {
        DateTime now = rtc.now();
        File f = SD.open("/datalog.csv", FILE_APPEND);
        if (f) {
            f.print(now.timestamp()); f.print(",");
            f.print(type); f.print(",");
            f.print(rssi); f.print(",");
            f.print(mac); f.print(",");
            f.print(gps.location.lat(), 6); f.print(",");
            f.println(gps.location.lng(), 6);
            f.close();
        }
    }
}

// ============================================================================
// SNIFFER CALLBACKS
// ============================================================================

void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    uint8_t *payload = (uint8_t *)ppkt->payload;
    
    // Simple SSID/MAC check logic
    char mac_str[18];
    sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", payload[10], payload[11], payload[12], payload[13], payload[14], payload[15]);
    
    bool match = false;
    for (int i=0; i<8; i++) {
        if (mac_str[0] == mac_prefixes[i][0]) { // Basic OUI check
            match = true; break;
        }
    }

    if (match) {
        log_detection("FLOCK_WIFI", ppkt->rx_ctrl.rssi, mac_str);
        beep(1000, 150);
    }
}

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) {
        if (device->getRSSI() > IGNORE_WEAK_RSSI) {
            String name = device->haveName() ? device->getName().c_str() : "BLE_DEV";
            for (int i=0; i<6; i++) {
                if (name.indexOf(wifi_ssid_patterns[i]) >= 0) {
                    log_detection("FLOCK_BLE", device->getRSSI(), device->getAddress().toString().c_str());
                    beep(1200, 150);
                }
            }
        }
    }
};

// ============================================================================
// UI SCREENS
// ============================================================================

void draw_header() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); 
    display.println(F("Flock-Detector 2.0"));
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.drawBitmap(118, 0, wifi_icon, 8, 8, SSD1306_WHITE);
}

void draw_scanner() {
    display.setCursor(0, 15);
    display.print("CH: "); display.print(current_channel);
    display.print(" SAT: "); display.print(gps.satellites.value());
    
    int y_min = 28, y_max = 52;
    display.drawFastVLine(scan_line_x, y_min, (y_max - y_min), SSD1306_BLACK);
    scan_line_x = (scan_line_x + 2) % 128;
    display.drawFastVLine(scan_line_x, y_min, (y_max - y_min), SSD1306_WHITE);
    
    display.setCursor(0, 56);
    display.drawBitmap(0, 56, clock_icon, 8, 8, SSD1306_WHITE);
    display.setCursor(12, 56);
    display.print(format_time((millis()-session_start_time)/1000));
}

void draw_live_feed() {
    for (int i=0; i<5; i++) {
        display.setCursor(0, 15 + (i*9));
        display.println(live_logs[i]);
    }
}

// ============================================================================
// MAIN
// ============================================================================

void setup() {
    Serial.begin(115200);
    GPS_SERIAL.begin(9600, SERIAL_8N1, D7, D6); 
    
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    Wire.begin();
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { Serial.println("OLED Fail"); }
    display.setRotation(2); // Flipped correctly
    display.clearDisplay();

    if (rtc.begin()) {
        if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    if (SD.begin(SD_CS_PIN)) {
        sd_available = true;
    }

    WiFi.mode(WIFI_STA); WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
    
    NimBLEDevice::init("");
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);

    session_start_time = millis();
    beep(400, 200); beep(800, 200);
}

void loop() {
    while (GPS_SERIAL.available() > 0) gps.encode(GPS_SERIAL.read());

    if (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - last_button_press > 300) {
            current_screen = (current_screen + 1) % 4;
            display.clearDisplay();
            last_button_press = millis();
        }
    }

    // Channel Hopping
    if (millis() - last_channel_hop > CHANNEL_HOP_INTERVAL) {
        current_channel = (current_channel % MAX_CHANNEL) + 1;
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        last_channel_hop = millis();
    }

    // BLE Scanning
    if (millis() - last_ble_scan > BLE_SCAN_INTERVAL) {
        pBLEScan->start(BLE_SCAN_DURATION, false);
        last_ble_scan = millis();
    }

    display.clearDisplay();
    draw_header();

    if (current_screen == 0) draw_scanner();
    else if (current_screen == 3) draw_live_feed();
    else {
        display.setCursor(0, 20);
        display.println("Stats/Cap Screen");
        display.print("WiFi: "); display.println(session_wifi);
        display.print("BLE: "); display.println(session_ble);
    }
    
    display.display();
    delay(10);
}