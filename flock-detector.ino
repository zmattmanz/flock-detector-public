#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// Hardware Config
#define BUZZER_PIN A3
#define SD_CS_PIN  D2
#define BUTTON_PIN D1
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

// Audio Config
#define LOW_FREQ 200
#define HIGH_FREQ 800
#define DETECT_FREQ 1000
#define HEARTBEAT_FREQ 600
#define BOOT_BEEP_DURATION 300
#define DETECT_BEEP_DURATION 150
#define HEARTBEAT_DURATION 100

// Scanning Config
#define MAX_CHANNEL 13
#define CHANNEL_HOP_INTERVAL 500
#define BLE_SCAN_DURATION 1
#define BLE_SCAN_INTERVAL 5000

// Live Feed Filters
#define LOG_UPDATE_DELAY 200  // Delay between log updates
#define IGNORE_WEAK_RSSI -80  // Show more devices (lowered threshold)

// Global Variables
static uint8_t current_channel = 1;
static unsigned long last_channel_hop = 0;
static unsigned long last_ble_scan = 0;
static bool triggered = false;
static bool device_in_range = false;
static unsigned long last_detection_time = 0;
static unsigned long last_heartbeat = 0;
static NimBLEScan* pBLEScan;
bool sd_available = false;

// UI State & Counters
int current_screen = 0; // 0=Scanner, 1=Stats, 2=Last Capture, 3=Live Log
unsigned long last_button_press = 0;

// Session Stats
long session_wifi = 0;
long session_ble = 0;
unsigned long session_start_time = 0;

// Lifetime Stats
long lifetime_wifi = 0;
long lifetime_ble = 0;
unsigned long lifetime_seconds = 0;

// Last Capture Data
String last_cap_type = "None";
String last_cap_mac = "--:--:--:--:--:--";
int last_cap_rssi = 0;
String last_cap_time = "00:00:00";

// Live Log Buffer
String live_logs[5] = {"", "", "", "", ""};

// UI Timers
unsigned long last_uptime_update = 0;
unsigned long last_anim_update = 0;
unsigned long last_stats_update = 0;
unsigned long last_time_save = 0;
unsigned long last_log_update = 0; 

// Animation Variables
int scan_line_x = 0;

// ============================================================================
// UI BITMAPS
// ============================================================================
const unsigned char wifi_icon[] PROGMEM = { 0x3C, 0x42, 0x81, 0x24, 0x18, 0x00, 0x18, 0x00 };
const unsigned char bt_icon[] PROGMEM =   { 0x10, 0x38, 0x54, 0x10, 0x10, 0x54, 0x38, 0x10 };
const unsigned char clock_icon[] PROGMEM ={ 0x3C, 0x42, 0x42, 0x52, 0x4A, 0x42, 0x3C, 0x00 };

// ============================================================================
// PATTERNS
// ============================================================================
static const char* wifi_ssid_patterns[] = { "flock", "Flock", "FLOCK", "FS Ext Battery", "Penguin", "Pigvision" };
static const char* mac_prefixes[] = { 
    "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84", "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
    "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc", "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea"
};
static const char* device_name_patterns[] = { "FS Ext Battery", "Penguin", "Flock", "Pigvision" };
#define RAVEN_GPS_SERVICE "00003100-0000-1000-8000-00805f9b34fb"
static const char* raven_service_uuids[] = { "0000180a-0000-1000-8000-00805f9b34fb", RAVEN_GPS_SERVICE };

// ============================================================================
// SYSTEM FUNCTIONS
// ============================================================================

void beep(int frequency, int duration_ms) {
    tone(BUZZER_PIN, frequency, duration_ms);
    delay(duration_ms + 50);
}

void boot_beep_sequence() {
    beep(LOW_FREQ, BOOT_BEEP_DURATION);
    beep(HIGH_FREQ, BOOT_BEEP_DURATION);
}

void load_lifetime_stats() {
    if (!sd_available) return;
    if (SD.exists("/stats.txt")) {
        File file = SD.open("/stats.txt", FILE_READ);
        if (file) {
            String data = file.readString();
            int firstComma = data.indexOf(',');
            int secondComma = data.indexOf(',', firstComma + 1);
            if (firstComma > 0) {
                lifetime_wifi = data.substring(0, firstComma).toInt();
                if (secondComma > 0) {
                    lifetime_ble = data.substring(firstComma + 1, secondComma).toInt();
                    lifetime_seconds = data.substring(secondComma + 1).toInt();
                } else {
                    lifetime_ble = data.substring(firstComma + 1).toInt();
                }
            }
            file.close();
        }
    }
}

void save_lifetime_stats() {
    if (!sd_available) return;
    File file = SD.open("/stats.txt", FILE_WRITE);
    if (file) {
        file.print(lifetime_wifi); file.print(",");
        file.print(lifetime_ble); file.print(",");
        file.print(lifetime_seconds);
        file.close();
    }
}

String format_time(unsigned long total_sec) {
    unsigned long m = (total_sec / 60) % 60;
    unsigned long h = (total_sec / 3600);
    if (h > 99) return String(h) + "h " + String(m) + "m";
    unsigned long s = total_sec % 60;
    char timeStr[10];
    sprintf(timeStr, "%02lu:%02lu:%02lu", h, m, s);
    return String(timeStr);
}

void add_to_live_log(String entry) {
    if (millis() - last_log_update < LOG_UPDATE_DELAY) return;
    for (int i = 4; i > 0; i--) {
        live_logs[i] = live_logs[i-1];
    }
    live_logs[0] = entry;
    last_log_update = millis();
}

String short_mac(String mac) {
    if (mac.length() > 8) return mac.substring(9);
    return mac;
}

// ============================================================================
// LOGGING & ALERTS
// ============================================================================

void flock_detected_beep_sequence() {
    Serial.println(F("FLOCK DETECTED!"));
    for (int i = 0; i < 3; i++) {
        display.invertDisplay(true);
        tone(BUZZER_PIN, DETECT_FREQ);
        delay(DETECT_BEEP_DURATION);
        noTone(BUZZER_PIN);
        display.invertDisplay(false);
        if (i < 2) delay(50);
    }
    device_in_range = true;
    last_detection_time = millis();
    last_heartbeat = millis();
}

void log_detection(const char* type, const char* proto, int rssi, const char* mac) {
    if (strcmp(proto, "WIFI") == 0) { session_wifi++; lifetime_wifi++; }
    else { session_ble++; lifetime_ble++; }

    last_cap_type = String(type);
    last_cap_mac = String(mac);
    last_cap_rssi = rssi;
    last_cap_time = format_time((millis()-session_start_time)/1000);

    String logEntry = String(type) + " (" + String(rssi) + ")";
    logEntry.replace("FLOCK_", ""); 
    
    for (int i = 4; i > 0; i--) { live_logs[i] = live_logs[i-1]; }
    live_logs[0] = "[!] " + logEntry;
    last_log_update = millis();

    save_lifetime_stats();

    if (sd_available) {
        File file = SD.open("/datalog.csv", FILE_APPEND);
        if (file) {
            file.print(millis()); file.print(",");
            file.print(type); file.print(",");
            file.print(proto); file.print(",");
            file.print(rssi); file.print(",");
            file.println(mac);
            file.close();
        }
    }
}

// ============================================================================
// DETECTION HELPERS
// ============================================================================
bool check_mac_prefix(const uint8_t* mac) {
    char mac_str[9]; snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
    for (int i = 0; i < sizeof(mac_prefixes)/sizeof(mac_prefixes[0]); i++) {
        if (strncasecmp(mac_str, mac_prefixes[i], 8) == 0) return true;
    } return false;
}
bool check_ssid_pattern(const char* ssid) {
    if (!ssid) return false;
    for (int i = 0; i < sizeof(wifi_ssid_patterns)/sizeof(wifi_ssid_patterns[0]); i++) {
        if (strcasestr(ssid, wifi_ssid_patterns[i])) return true;
    } return false;
}
bool check_device_name_pattern(const char* name) {
    if (!name) return false;
    for (int i = 0; i < sizeof(device_name_patterns)/sizeof(device_name_patterns[0]); i++) {
        if (strcasestr(name, device_name_patterns[i])) return true;
    } return false;
}
bool check_raven_service_uuid(NimBLEAdvertisedDevice* device) {
    if (!device || !device->haveServiceUUID()) return false;
    int count = device->getServiceUUIDCount();
    for (int i = 0; i < count; i++) {
        std::string uuid = device->getServiceUUID(i).toString();
        for (int j = 0; j < sizeof(raven_service_uuids)/sizeof(raven_service_uuids[0]); j++) {
            if (strcasecmp(uuid.c_str(), raven_service_uuids[j]) == 0) return true;
        }
    } return false;
}

void output_wifi_detection_json(const char* ssid, const uint8_t* mac, int rssi, const char* detection_type) {
    char mac_str[18]; snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    log_detection("FLOCK_WIFI", "WIFI", rssi, mac_str);
}
void output_ble_detection_json(const char* mac, const char* name, int rssi, const char* detection_method) {
    log_detection("FLOCK_BLE", "BLE", rssi, mac);
}

// ============================================================================
// UI SCREENS
// ============================================================================

void draw_header() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); 
    display.println(F("Flock Detection"));
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.drawBitmap(105, 0, bt_icon, 6, 8, SSD1306_WHITE);
    display.drawBitmap(118, 0, wifi_icon, 8, 8, SSD1306_WHITE);
}

void update_animation() {
    int y_min = 28; int y_max = 52;
    for(int i=0; i<3; i++) {
        display.drawPixel(random(0, 128), random(y_min, y_max), SSD1306_BLACK);
        display.drawPixel(random(0, 128), random(y_min, y_max), SSD1306_WHITE);
    }
    display.drawFastVLine(scan_line_x, y_min, (y_max - y_min), SSD1306_BLACK);
    scan_line_x += 2; if (scan_line_x > 128) scan_line_x = 0;
    display.drawFastVLine(scan_line_x, y_min, (y_max - y_min), SSD1306_WHITE);
    display.display();
}

void draw_scanner_screen() {
    if (millis() - last_uptime_update > 1000) {
        display.fillRect(0, 56, 128, 8, SSD1306_BLACK);
        display.drawBitmap(0, 56, clock_icon, 8, 8, SSD1306_WHITE);
        display.setCursor(12, 56);
        unsigned long session_sec = (millis() - session_start_time) / 1000;
        display.print(format_time(session_sec));
        if(sd_available) { display.setCursor(100, 56); display.print(F("SD:OK")); }
        display.display();
        last_uptime_update = millis();
    }
}

void draw_stats_screen() {
    if (millis() - last_stats_update > 500) {
        display.clearDisplay();
        draw_header();
        display.setCursor(0, 13); display.print(F("Scanner Stats"));
        display.setCursor(40, 24); display.print(F("SESS"));
        display.setCursor(80, 24); display.print(F("TOTAL"));
        display.setCursor(0, 34); display.print(F("WiFi:"));
        display.setCursor(40, 34); display.print(session_wifi);
        display.setCursor(80, 34); display.print(lifetime_wifi);
        display.setCursor(0, 44); display.print(F("BLE:"));
        display.setCursor(40, 44); display.print(session_ble);
        display.setCursor(80, 44); display.print(lifetime_ble);
        display.drawLine(0, 53, 128, 53, SSD1306_WHITE);
        display.setCursor(0, 56); display.print(F("Run Time: "));
        display.print(format_time(lifetime_seconds));
        display.display();
        last_stats_update = millis();
    }
}

void draw_last_capture_screen() {
    if (millis() - last_stats_update > 500) {
        display.clearDisplay();
        draw_header();
        display.setCursor(0, 13); display.print(F("Last Capture"));
        if (last_cap_type == "None") {
            display.setCursor(30, 35); display.print(F("NO DATA YET"));
        } else {
            display.setCursor(0, 25); display.print(F("Time: ")); display.print(last_cap_time);
            display.setCursor(0, 35); display.print(F("Type: ")); display.print(last_cap_type);
            display.setCursor(0, 45); display.print(F("RSSI: ")); display.print(last_cap_rssi);
            display.setCursor(0, 55); display.print(last_cap_mac);
        }
        display.display();
        last_stats_update = millis();
    }
}

void draw_live_log_screen() {
    if (millis() - last_stats_update > 100) {
        display.clearDisplay();
        draw_header();
        
        display.setCursor(0, 13);
        display.print(F("Live Feed"));
        
        int y = 24;
        for (int i = 0; i < 5; i++) {
            if (live_logs[i] != "") {
                display.setCursor(0, y);
                if (live_logs[i].startsWith("[!]")) display.setTextColor(SSD1306_INVERSE);
                else display.setTextColor(SSD1306_WHITE);
                
                display.print(live_logs[i]);
                display.setTextColor(SSD1306_WHITE);
                y += 8;
            }
        }
        display.display();
        last_stats_update = millis();
    }
}

void refresh_screen_layout() {
    display.clearDisplay();
    draw_header();
    display.display();
}

// ============================================================================
// PACKET HANDLERS
// ============================================================================
typedef struct {
    unsigned frame_ctrl:16; unsigned duration_id:16;
    uint8_t addr1[6]; uint8_t addr2[6]; uint8_t addr3[6];
    unsigned sequence_ctrl:16; uint8_t addr4[6];
} wifi_ieee80211_mac_hdr_t;

typedef struct {
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0];
} wifi_ieee80211_packet_t;

void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
    uint8_t frame_type = (hdr->frame_ctrl & 0xFF) >> 2;
    if (frame_type != 0x20 && frame_type != 0x80) return;
    char ssid[33] = {0}; uint8_t *payload = (uint8_t *)ipkt + 24;
    if (frame_type == 0x20) payload += 0; else payload += 12;
    if (payload[0] == 0 && payload[1] <= 32) { memcpy(ssid, &payload[2], payload[1]); ssid[payload[1]] = '\0'; }
    
    bool match = false;
    if (strlen(ssid) > 0 && check_ssid_pattern(ssid)) match = true;
    else if (check_mac_prefix(hdr->addr2)) match = true;

    if (match) {
        String logEntry = "[!] " + (strlen(ssid)>0 ? String(ssid) : "Hidden");
        logEntry += " ("; logEntry += ppkt->rx_ctrl.rssi; logEntry += ")";
        
        for (int i = 4; i > 0; i--) { live_logs[i] = live_logs[i-1]; }
        live_logs[0] = logEntry;
        
        char mac_str[18]; snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
        log_detection("FLOCK_WIFI", "WIFI", ppkt->rx_ctrl.rssi, mac_str);
        if (!triggered) { triggered = true; flock_detected_beep_sequence(); }
        last_detection_time = millis();
    } 
    else {
        if (ppkt->rx_ctrl.rssi > IGNORE_WEAK_RSSI) {
             String logEntry = ""; 
             if (strlen(ssid) > 0) {
                 logEntry += String(ssid);
             } else {
                 char mac_short[9]; 
                 snprintf(mac_short, sizeof(mac_short), "%02x:%02x:%02x", hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
                 logEntry += "WiFi:"; logEntry += mac_short;
             }
             logEntry += " ("; logEntry += ppkt->rx_ctrl.rssi; logEntry += ")";
             add_to_live_log(logEntry);
        }
    }
}

void hop_channel() {
    unsigned long now = millis();
    if (now - last_channel_hop > CHANNEL_HOP_INTERVAL) {
        current_channel++;
        if (current_channel > MAX_CHANNEL) current_channel = 1;
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        last_channel_hop = now;
        if (current_screen == 0) {
            display.fillRect(0, 16, 128, 10, SSD1306_BLACK); 
            display.setCursor(0, 16);
            display.print(F("Scanning: ")); display.print(current_channel); display.print(F(" (WiFi)"));
            display.display();
        }
    }
}

class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        NimBLEAddress addr = advertisedDevice->getAddress();
        uint8_t mac[6];
        sscanf(addr.toString().c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        
        bool match = false;
        if (check_mac_prefix(mac)) match = true;
        else if (advertisedDevice->haveName() && check_device_name_pattern(advertisedDevice->getName().c_str())) match = true;
        else if (check_raven_service_uuid(advertisedDevice)) match = true;

        if (match) {
             String logEntry = "[!] " + (advertisedDevice->haveName() ? String(advertisedDevice->getName().c_str()) : "Unknown BLE");
             logEntry += " ("; logEntry += advertisedDevice->getRSSI(); logEntry += ")";
             
             for (int i = 4; i > 0; i--) { live_logs[i] = live_logs[i-1]; }
             live_logs[0] = logEntry;

             log_detection("FLOCK_BLE", "BLE", advertisedDevice->getRSSI(), addr.toString().c_str());
             if (!triggered) { triggered = true; flock_detected_beep_sequence(); }
             last_detection_time = millis();
        } 
        else {
             if (advertisedDevice->getRSSI() > IGNORE_WEAK_RSSI) {
                 String logEntry = ""; 
                 if (advertisedDevice->haveName()) {
                     logEntry += advertisedDevice->getName().c_str();
                 } else {
                     logEntry += "BLE:";
                     logEntry += short_mac(String(addr.toString().c_str()));
                 }
                 
                 logEntry += " ("; logEntry += advertisedDevice->getRSSI(); logEntry += ")";
                 add_to_live_log(logEntry);
             }
        }
    }
};

// ============================================================================
// MAIN SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(SD_CS_PIN, OUTPUT); digitalWrite(SD_CS_PIN, HIGH); 
    SPI.begin(); 

    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { Serial.println(F("SSD1306 allocation failed")); }
    display.setRotation(2);
    
    bool mount_success = false;
    for(int i=0; i<3; i++) { if (SD.begin(SD_CS_PIN)) { mount_success = true; break; } delay(100); }
    if (mount_success) {
        sd_available = true;
        load_lifetime_stats();
        if (!SD.exists("/datalog.csv")) {
            File file = SD.open("/datalog.csv", FILE_WRITE);
            if (file) { file.println("Timestamp_ms,Type,Protocol,RSSI,MAC_Address"); file.close(); }
        }
    }
    
    session_start_time = millis();
    refresh_screen_layout();

    WiFi.mode(WIFI_STA); WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
    esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
    NimBLEDevice::init("");
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); pBLEScan->setInterval(100); pBLEScan->setWindow(99);

    boot_beep_sequence();
    last_channel_hop = millis();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - last_button_press > 300) {
            current_screen++;
            if (current_screen > 3) current_screen = 0; // 0->1->2->3->0
            refresh_screen_layout();
            last_button_press = millis();
        }
    }

    if (millis() - last_time_save >= 1000) {
        lifetime_seconds++;
        last_time_save = millis();
        if (lifetime_seconds % 60 == 0) save_lifetime_stats();
    }

    hop_channel();
    
    if (millis() - last_ble_scan >= BLE_SCAN_INTERVAL) {
        if (!pBLEScan->isScanning()) {
            if (current_screen == 0) {
                display.fillRect(0, 16, 128, 10, SSD1306_BLACK);
                display.setCursor(0, 16);
                display.print(F("Scanning: BLE..."));
                display.display();
            }
            pBLEScan->start(BLE_SCAN_DURATION, false);
            last_ble_scan = millis();
        }
    }
    if (!pBLEScan->isScanning() && (millis() - last_ble_scan > 1000)) pBLEScan->clearResults();

    if (current_screen == 0) {
        draw_scanner_screen();
        if (millis() - last_anim_update > 40) { update_animation(); last_anim_update = millis(); }
    } else if (current_screen == 1) {
        draw_stats_screen();
    } else if (current_screen == 2) {
        draw_last_capture_screen();
    } else if (current_screen == 3) {
        draw_live_log_screen();
    }
    
    delay(10);
}