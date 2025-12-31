/*
    ESP32-S3 TSL Tally Light with Web Configuration
    Video Walrus 2025
*/

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#define BUFFER_LENGTH 256
#define NUM_LEDS 7
#define DATA_PIN 16
#define RESET_BUTTON_PIN 0  // GPIO 0 (BOOT button) for factory reset
#define WIFI_CONNECT_TIMEOUT 10000  // 10 seconds to connect to WiFi
#define FIRMWARE_VERSION "1.0.2"
#define MAX_DISCOVERED_DEVICES 16

// W5500 SPI Ethernet configuration - MUST be defined BEFORE including ETH.h
#define ETH_PHY_TYPE    ETH_PHY_W5500
#define ETH_PHY_ADDR    1
#define ETH_PHY_CS      14
#define ETH_PHY_IRQ     -1
#define ETH_PHY_RST     9
#define ETH_PHY_SPI_HOST SPI2_HOST
#define ETH_PHY_SPI_SCK  13
#define ETH_PHY_SPI_MISO 12
#define ETH_PHY_SPI_MOSI 11

#include <ETH.h>
#include <SPI.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

// GitHub OTA Update configuration
#define GITHUB_REPO "videojedi/esp32-s3-tally"
#define GITHUB_API_URL "https://api.github.com/repos/videojedi/esp32-s3-tally/releases/latest"

// Forward declarations
void loadSettings();
void saveSettings();
void resetSettings();
void checkResetButton();
void onEvent(arduino_event_id_t event);
void setupWebServer();
String getConfigPage();
void udpTSL(char *data);
void setTallyState(int state);
bool setupWiFi();
void startAP();
String getActiveIP();
void startUDP();
void stopUDP();
void udpListenerTask(void *pvParameters);
void startUDPTask();
void stopUDPTask();
void startMDNS();
void testLED();
void discoverTallyDevices();
String getDefaultHostname();

// Web server
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// Configurable settings (loaded from NVS)
int tslAddress = 0;
int maxBrightness = 50;  // Max brightness (0-255), TSL brightness maps to this
int tslPort = 8901;      // TSL multicast port
String tslMulticast = "239.1.2.3";  // TSL multicast address
bool useDHCP = true;
String staticIP = "192.168.1.100";
String gateway = "192.168.1.1";
String subnet = "255.255.255.0";
String dns = "8.8.8.8";
String deviceHostname = "ESP-TSL-Tally";

// WiFi settings
String wifiSSID = "";
String wifiPassword = "";
bool wifiEnabled = false;

// AP settings
String apSSID = "TSL-Tally-Setup";
String apPassword = "tallytally";

bool redTally = false;
int redLED = false;
bool greenTally = false;
int greenLED = false;

IPAddress multicastAddress;

// Synchronous UDP for dedicated task
NetworkUDP udp;

// FreeRTOS task handle for UDP listener
TaskHandle_t udpTaskHandle = NULL;
volatile bool udpRunning = false;

CRGB leds[NUM_LEDS];

static bool eth_connected = false;
static bool wifi_connected = false;
static bool ap_mode = false;
static bool discoMode = false;
static unsigned long discoEndTime = 0;
String currentTallyState = "Off";
String currentTallyText = "";

// Structure for discovered tally devices
struct TallyDevice {
  String hostname;
  String ip;
  int tslAddress;
  String tallyState;
  unsigned long lastSeen;
  bool online;
};

TallyDevice discoveredDevices[MAX_DISCOVERED_DEVICES];
int numDiscoveredDevices = 0;
unsigned long lastDiscoveryScan = 0;

// GitHub OTA update state
String latestVersion = "";
String firmwareURL = "";
bool updateAvailable = false;
bool updateInProgress = false;

// Generate unique default hostname using ESP32 base MAC address
String getDefaultHostname() {
  uint64_t chipid = ESP.getEfuseMac();  // Factory-programmed MAC, always available
  char hostname[20];
  // Use last 3 bytes of MAC for uniqueness: Tally-XXYYZZ
  snprintf(hostname, sizeof(hostname), "Tally-%02X%02X%02X",
           (uint8_t)(chipid >> 24), (uint8_t)(chipid >> 32), (uint8_t)(chipid >> 40));
  return String(hostname);
}

// Load settings from NVS
void loadSettings() {
  preferences.begin("tally", true);  // read-only
  tslAddress = preferences.getInt("tslAddress", 0);
  maxBrightness = preferences.getInt("maxBright", 50);
  tslPort = preferences.getInt("tslPort", 8901);
  tslMulticast = preferences.getString("tslMcast", "239.1.2.3");
  useDHCP = preferences.getBool("useDHCP", true);
  staticIP = preferences.getString("staticIP", "192.168.1.100");
  gateway = preferences.getString("gateway", "192.168.1.1");
  subnet = preferences.getString("subnet", "255.255.255.0");
  dns = preferences.getString("dns", "8.8.8.8");
  deviceHostname = preferences.getString("hostname", getDefaultHostname());
  wifiSSID = preferences.getString("wifiSSID", "");
  wifiPassword = preferences.getString("wifiPass", "");
  wifiEnabled = preferences.getBool("wifiEnabled", false);
  preferences.end();

  Serial.println("Settings loaded:");
  Serial.printf("  TSL Address: %d\n", tslAddress);
  Serial.printf("  TSL Multicast: %s\n", tslMulticast.c_str());
  Serial.printf("  TSL Port: %d\n", tslPort);
  Serial.printf("  Max Brightness: %d\n", maxBrightness);
  Serial.printf("  DHCP: %s\n", useDHCP ? "Yes" : "No");
  if (!useDHCP) {
    Serial.printf("  Static IP: %s\n", staticIP.c_str());
    Serial.printf("  Gateway: %s\n", gateway.c_str());
    Serial.printf("  Subnet: %s\n", subnet.c_str());
    Serial.printf("  DNS: %s\n", dns.c_str());
  }
  Serial.printf("  Hostname: %s\n", deviceHostname.c_str());
  Serial.printf("  WiFi Enabled: %s\n", wifiEnabled ? "Yes" : "No");
  if (wifiEnabled && wifiSSID.length() > 0) {
    Serial.printf("  WiFi SSID: %s\n", wifiSSID.c_str());
  }
}

// Save settings to NVS
void saveSettings() {
  preferences.begin("tally", false);  // read-write
  preferences.putInt("tslAddress", tslAddress);
  preferences.putInt("maxBright", maxBrightness);
  preferences.putInt("tslPort", tslPort);
  preferences.putString("tslMcast", tslMulticast);
  preferences.putBool("useDHCP", useDHCP);
  preferences.putString("staticIP", staticIP);
  preferences.putString("gateway", gateway);
  preferences.putString("subnet", subnet);
  preferences.putString("dns", dns);
  preferences.putString("hostname", deviceHostname);
  preferences.putString("wifiSSID", wifiSSID);
  preferences.putString("wifiPass", wifiPassword);
  preferences.putBool("wifiEnabled", wifiEnabled);
  preferences.end();
  Serial.println("Settings saved to NVS");
}

// Reset settings to factory defaults
void resetSettings() {
  preferences.begin("tally", false);
  preferences.clear();
  preferences.end();
  Serial.println("Settings reset to factory defaults");

  // Reset to defaults in memory
  tslAddress = 0;
  maxBrightness = 50;
  tslPort = 8901;
  tslMulticast = "239.1.2.3";
  useDHCP = true;
  staticIP = "192.168.1.100";
  gateway = "192.168.1.1";
  subnet = "255.255.255.0";
  dns = "8.8.8.8";
  deviceHostname = getDefaultHostname();
  wifiSSID = "";
  wifiPassword = "";
  wifiEnabled = false;
}

// Check if reset button is held during boot
void checkResetButton() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("Reset button pressed, hold for 3 seconds to reset...");
    unsigned long startTime = millis();

    // Flash LEDs to indicate reset mode
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
      if (millis() - startTime > 3000) {
        Serial.println("Resetting to factory defaults!");
        fill_solid(leds, NUM_LEDS, CRGB::Blue);
        FastLED.show();
        resetSettings();
        delay(1000);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        break;
      }
      // Blink red while waiting
      fill_solid(leds, NUM_LEDS, ((millis() / 200) % 2) ? CRGB::Red : CRGB::Black);
      FastLED.show();
      delay(50);
    }
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
}

// WARNING: onEvent is called from a separate FreeRTOS task (thread)!
void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      // The hostname must be set after the interface is started, but needs
      // to be set before DHCP, so set it from the event handler thread.
      ETH.setHostname(deviceHostname.c_str());
      break;
    case ARDUINO_EVENT_ETH_CONNECTED: Serial.println("ETH Connected"); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("ETH Got IP");
      Serial.println(ETH);
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("ETH Lost IP");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("WiFi Got IP");
      Serial.println(WiFi.localIP());
      wifi_connected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi Disconnected");
      wifi_connected = false;
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("AP Started");
      ap_mode = true;
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("AP Stopped");
      ap_mode = false;
      break;
    default: break;
  }
}

// Get the active IP address (Ethernet preferred, then WiFi, then AP)
String getActiveIP() {
  if (eth_connected) {
    return ETH.localIP().toString();
  } else if (wifi_connected) {
    return WiFi.localIP().toString();
  } else if (ap_mode) {
    return WiFi.softAPIP().toString();
  }
  return "0.0.0.0";
}

// Get connection status string
String getConnectionStatus() {
  String status = "";
  if (eth_connected) status += "Ethernet";
  if (wifi_connected) {
    if (status.length() > 0) status += " + ";
    status += "WiFi";
  }
  if (ap_mode) {
    if (status.length() > 0) status += " + ";
    status += "AP";
  }
  if (status.length() == 0) status = "Disconnected";
  return status;
}

// Try to connect to WiFi
bool setupWiFi() {
  if (!wifiEnabled || wifiSSID.length() == 0) {
    Serial.println("WiFi not configured or disabled");
    return false;
  }

  Serial.printf("Connecting to WiFi: %s\n", wifiSSID.c_str());

  // Make sure WiFi is in a clean state
  WiFi.disconnect(true);
  delay(100);
  yield();

  WiFi.setHostname(deviceHostname.c_str());
  WiFi.mode(WIFI_STA);
  delay(100);
  yield();

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
    yield();  // Feed the watchdog
    delay(100);
    Serial.print(".");
    // Blink purple while connecting
    fill_solid(leds, NUM_LEDS, ((millis() / 300) % 2) ? CRGB::Purple : CRGB::Black);
    FastLED.show();
  }
  Serial.println();

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    wifi_connected = true;
    return true;
  } else {
    Serial.println("WiFi connection failed");
    WiFi.disconnect(true);
    return false;
  }
}

// Start Access Point for configuration
void startAP() {
  // Flash white briefly to show we're about to start AP
  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
  delay(100);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Disconnect any existing WiFi first
  WiFi.disconnect(true);
  delay(100);

  // Set AP mode first so MAC address is available
  WiFi.mode(WIFI_AP);
  delay(100);  // Let mode change settle

  // Generate unique AP SSID using MAC address (must be after WiFi.mode)
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);  // Use softAP MAC for AP mode
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "Tally-%02X%02X%02X-Setup", mac[3], mac[4], mac[5]);
  apSSID = String(ssid);

  Serial.printf("Starting AP: %s\n", apSSID.c_str());

  bool apStarted = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
  delay(500);  // Give AP time to fully initialize

  if (apStarted) {
    ap_mode = true;
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("AP started! IP: %s\n", apIP.toString().c_str());

    // Start DNS server for captive portal (redirect all domains to our IP)
    dnsServer.start(53, "*", apIP);
    Serial.println("Captive portal DNS started");

    // Blink cyan to indicate AP mode
    for (int i = 0; i < 3; i++) {
      fill_solid(leds, NUM_LEDS, CRGB::Cyan);
      FastLED.show();
      delay(200);
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      delay(200);
      yield();  // Feed watchdog
    }
    // Leave LED dim cyan to show AP mode is active
    FastLED.setBrightness(10);
    fill_solid(leds, NUM_LEDS, CRGB::Cyan);
    FastLED.show();
  } else {
    Serial.println("ERROR: Failed to start AP!");
    // Blink red to indicate error
    for (int i = 0; i < 5; i++) {
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      delay(100);
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      delay(100);
    }
  }
}

// Set tally state directly (used by both TSL and test buttons)
void setTallyState(int state) {
  FastLED.setBrightness(maxBrightness);
  switch (state) {
    case 0:
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      currentTallyState = "Off";
      Serial.println("Tally: Off");
      break;
    case 1:
      fill_solid(leds, NUM_LEDS, CRGB::Green);
      currentTallyState = "Green";
      Serial.println("Tally: Green");
      break;
    case 2:
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      currentTallyState = "Red";
      Serial.println("Tally: Red");
      break;
    case 3:
      fill_solid(leds, NUM_LEDS, CRGB::Yellow);
      currentTallyState = "Yellow";
      Serial.println("Tally: Yellow");
      break;
    default:
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      currentTallyState = "Off";
      Serial.println("Tally: Off*");
  }
  FastLED.show();
}

void udpTSL(char *data) {
  char* message;
  int T;
  int Bright;
  int addr;
  String text;
  message = data;

  addr = message[0] - 128;

  if (tslAddress == addr) {
    T = message[1] & 0b00001111;

    for (int j = 2; j < 17; j++) {
      text += message[j];
    }
    text.trim();  // Remove trailing spaces
    currentTallyText = text;
    Serial.printf("Text: %s\n", text.c_str());

    Bright = message[1] & 0b00110000;
    Bright = Bright >> 4;
    Bright = map(Bright, 0, 3, 0, maxBrightness);
    Serial.printf("Brightness: %d\n", Bright);
    FastLED.setBrightness(Bright);

    setTallyState(T);
  }
}

// Start UDP multicast listener
void startUDP() {
  if (udpRunning) return;

  Serial.println("Joining multicast group...");
  if (udp.beginMulticast(multicastAddress, tslPort)) {
    Serial.printf("UDP multicast listening on %s:%d\n",
                  multicastAddress.toString().c_str(), tslPort);
    udpRunning = true;
  } else {
    Serial.println("Failed to start multicast UDP!");
  }
}

// Stop UDP listener
void stopUDP() {
  if (udpRunning) {
    udp.stop();
    udpRunning = false;
    Serial.println("[UDP] Stopped");
  }
}

// UDP listener task - runs on core 0 for reliable multicast reception
void udpListenerTask(void *pvParameters) {
  Serial.printf("[UDP Task] Running on core %d\n", xPortGetCoreID());

  char buffer[256];

  for (;;) {
    if (udpRunning) {
      int packetSize = udp.parsePacket();
      if (packetSize) {
        IPAddress remote = udp.remoteIP();
        uint16_t port = udp.remotePort();

        int len = udp.read(buffer, sizeof(buffer) - 1);
        if (len > 0) {
          buffer[len] = '\0';
          Serial.printf("[UDP] From %s:%d, Length: %d\n",
                        remote.toString().c_str(), port, len);
          udpTSL(buffer);
        }
      }
    }
    // Small delay to yield CPU time
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Start UDP listener task on core 0
void startUDPTask() {
  if (udpTaskHandle != NULL) {
    Serial.println("[UDP] Task already running");
    return;
  }

  // Start UDP first, then the task
  startUDP();

  xTaskCreatePinnedToCore(
    udpListenerTask,   // Task function
    "UDP Task",        // Name
    4096,              // Stack size
    NULL,              // Parameters
    1,                 // Priority
    &udpTaskHandle,    // Task handle
    0                  // Core 0 (main loop runs on core 1)
  );
  Serial.println("[UDP] Task started on core 0");
}

// Stop UDP listener task
void stopUDPTask() {
  if (udpTaskHandle != NULL) {
    vTaskDelete(udpTaskHandle);
    udpTaskHandle = NULL;
    Serial.println("[UDP] Task stopped");
  }
  stopUDP();
}

// Start mDNS responder with TXT records for device discovery
void startMDNS() {
  if (MDNS.begin(deviceHostname.c_str())) {
    Serial.printf("mDNS responder started: http://%s.local\n", deviceHostname.c_str());
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("tally", "tcp", 80);  // Custom service for tally discovery

    // Add TXT records for device info (used by discovery)
    MDNS.addServiceTxt("tally", "tcp", "tsladdr", String(tslAddress));
    MDNS.addServiceTxt("tally", "tcp", "version", FIRMWARE_VERSION);
    MDNS.addServiceTxt("tally", "tcp", "mac", eth_connected ? ETH.macAddress() : WiFi.macAddress());
  } else {
    Serial.println("mDNS start failed");
  }
}

// Discover other tally devices on the network via mDNS
void discoverTallyDevices() {
  Serial.println("[Discovery] Scanning for tally devices...");
  numDiscoveredDevices = 0;

  int n = MDNS.queryService("tally", "tcp");
  Serial.printf("[Discovery] Found %d tally service(s)\n", n);

  for (int i = 0; i < n && numDiscoveredDevices < MAX_DISCOVERED_DEVICES; i++) {
    String foundIP = MDNS.address(i).toString();
    String myIP = getActiveIP();

    // Skip ourselves
    if (foundIP == myIP) {
      Serial.printf("[Discovery] Skipping self: %s\n", foundIP.c_str());
      continue;
    }

    discoveredDevices[numDiscoveredDevices].hostname = MDNS.hostname(i);
    discoveredDevices[numDiscoveredDevices].ip = foundIP;
    discoveredDevices[numDiscoveredDevices].lastSeen = millis();
    discoveredDevices[numDiscoveredDevices].online = true;

    // Try to get TSL address from TXT record
    int txtCount = MDNS.numTxt(i);
    for (int j = 0; j < txtCount; j++) {
      if (MDNS.txtKey(i, j) == "tsladdr") {
        discoveredDevices[numDiscoveredDevices].tslAddress = MDNS.txt(i, j).toInt();
      }
    }

    Serial.printf("[Discovery] Found: %s at %s (TSL:%d)\n",
                  discoveredDevices[numDiscoveredDevices].hostname.c_str(),
                  discoveredDevices[numDiscoveredDevices].ip.c_str(),
                  discoveredDevices[numDiscoveredDevices].tslAddress);

    numDiscoveredDevices++;
  }

  lastDiscoveryScan = millis();
  Serial.printf("[Discovery] Total devices found: %d\n", numDiscoveredDevices);
}

// Compare version strings (returns true if v2 > v1)
bool isNewerVersion(const String& v1, const String& v2) {
  // Strip 'v' prefix if present
  String ver1 = v1.startsWith("v") ? v1.substring(1) : v1;
  String ver2 = v2.startsWith("v") ? v2.substring(1) : v2;

  // Simple string comparison works for semantic versioning
  // e.g., "1.0.1" > "1.0.0", "1.1.0" > "1.0.9"
  int parts1[3] = {0, 0, 0};
  int parts2[3] = {0, 0, 0};

  sscanf(ver1.c_str(), "%d.%d.%d", &parts1[0], &parts1[1], &parts1[2]);
  sscanf(ver2.c_str(), "%d.%d.%d", &parts2[0], &parts2[1], &parts2[2]);

  for (int i = 0; i < 3; i++) {
    if (parts2[i] > parts1[i]) return true;
    if (parts2[i] < parts1[i]) return false;
  }
  return false;
}

// Check GitHub for firmware updates
void checkForUpdates() {
  if (!eth_connected && !wifi_connected) {
    Serial.println("[Update] No network connection");
    return;
  }

  Serial.println("[Update] Checking GitHub for updates...");

  // Use WiFiClientSecure for HTTPS
  WiFiClientSecure client;
  client.setInsecure();  // Skip certificate verification

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  http.begin(client, GITHUB_API_URL);
  http.addHeader("User-Agent", "ESP32-Tally-OTA");
  http.addHeader("Accept", "application/vnd.github.v3+json");

  int httpCode = http.GET();
  Serial.printf("[Update] GitHub API response: %d\n", httpCode);

  if (httpCode == 200) {
    String payload = http.getString();

    // Parse tag_name for version
    int tagStart = payload.indexOf("\"tag_name\":\"");
    if (tagStart > 0) {
      tagStart += 12;
      int tagEnd = payload.indexOf("\"", tagStart);
      latestVersion = payload.substring(tagStart, tagEnd);
      Serial.printf("[Update] Latest version: %s, Current: %s\n",
                    latestVersion.c_str(), FIRMWARE_VERSION);
    }

    // Find firmware.bin in assets
    int assetsStart = payload.indexOf("\"assets\":");
    if (assetsStart > 0) {
      int binStart = payload.indexOf("\"browser_download_url\":", assetsStart);
      while (binStart > 0) {
        binStart += 24;
        int binEnd = payload.indexOf("\"", binStart);
        String url = payload.substring(binStart, binEnd);
        if (url.endsWith("firmware.bin")) {
          firmwareURL = url;
          Serial.printf("[Update] Firmware URL: %s\n", firmwareURL.c_str());
          break;
        }
        binStart = payload.indexOf("\"browser_download_url\":", binEnd);
      }
    }

    // Check if update is available
    if (latestVersion.length() > 0 && isNewerVersion(FIRMWARE_VERSION, latestVersion)) {
      updateAvailable = true;
      Serial.println("[Update] New version available!");
    } else {
      updateAvailable = false;
      Serial.println("[Update] Firmware is up to date");
    }
  } else {
    Serial.printf("[Update] Failed to check for updates: %d\n", httpCode);
  }

  http.end();
}

// Perform OTA update from GitHub
void performOTAUpdate() {
  if (firmwareURL.length() == 0) {
    Serial.println("[Update] No firmware URL available");
    return;
  }

  if (updateInProgress) {
    Serial.println("[Update] Update already in progress");
    return;
  }

  updateInProgress = true;
  Serial.printf("[Update] Downloading firmware from: %s\n", firmwareURL.c_str());

  // Show update in progress on LEDs
  fill_solid(leds, NUM_LEDS, CRGB::Purple);
  FastLED.show();

  // Use WiFiClientSecure for HTTPS
  WiFiClientSecure client;
  client.setInsecure();  // Skip certificate verification for GitHub

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(60000);  // 60 second timeout for large downloads
  http.begin(client, firmwareURL);

  Serial.println("[Update] Starting download...");
  int httpCode = http.GET();
  Serial.printf("[Update] Download response: %d\n", httpCode);

  if (httpCode == 200) {
    int contentLength = http.getSize();
    Serial.printf("[Update] Firmware size: %d bytes\n", contentLength);

    if (contentLength > 0) {
      if (Update.begin(contentLength)) {
        Serial.println("[Update] Starting OTA flash...");

        size_t written = Update.writeStream(client);
        Serial.printf("[Update] Written: %d bytes\n", written);

        if (Update.end()) {
          if (Update.isFinished()) {
            Serial.println("[Update] Update successful! Rebooting...");
            fill_solid(leds, NUM_LEDS, CRGB::Green);
            FastLED.show();
            delay(1000);
            ESP.restart();
          } else {
            Serial.println("[Update] Update not finished");
          }
        } else {
          Serial.printf("[Update] Update error: %s\n", Update.errorString());
        }
      } else {
        Serial.printf("[Update] Not enough space: %s\n", Update.errorString());
      }
    } else {
      Serial.println("[Update] Invalid content length");
    }
  } else {
    Serial.printf("[Update] Download failed: %d\n", httpCode);
  }

  http.end();
  updateInProgress = false;

  // Restore LED state on failure
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(2000);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// LED test routine - cycles through R/G/B
void testLED() {
  FastLED.setBrightness(maxBrightness);
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// HTML page for configuration
String getConfigPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<link rel=\"icon\" href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='45' fill='%23ff0000'/></svg>\">";
  html += "<title>TSL Tally Configuration</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:20px;background:#1a1a2e;color:#eee}";
  html += ".container{max-width:500px;margin:0 auto}";
  html += "h1{color:#00d4ff;text-align:center}";
  html += ".card{background:#16213e;padding:20px;border-radius:10px;margin-bottom:20px}";
  html += ".card h2{margin-top:0;color:#00d4ff;border-bottom:1px solid #0f3460;padding-bottom:10px}";
  html += "label{display:block;margin:10px 0 5px;font-weight:bold}";
  html += "input[type=text],input[type=number],input[type=password],select{width:100%;padding:10px;border:1px solid #0f3460;border-radius:5px;background:#0f3460;color:#eee;box-sizing:border-box}";
  html += "input:focus,select:focus{outline:none;border-color:#00d4ff}";
  html += ".ip-fields,.wifi-fields{display:none}.ip-fields.show,.wifi-fields.show{display:block}";
  html += "button{width:100%;padding:15px;background:#00d4ff;color:#1a1a2e;border:none;border-radius:5px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:20px}";
  html += "button:hover{background:#00b4d8}";
  html += ".test-btns{display:flex;gap:10px;margin-top:10px}";
  html += ".test-btn{flex:1;padding:15px 10px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;font-size:14px}";
  html += ".test-btn:hover{opacity:0.8}";
  html += ".btn-green{background:#0f0;color:#000}.btn-red{background:#f00;color:#fff}.btn-yellow{background:#ff0;color:#000}";
  html += ".status{background:#0f3460;padding:15px;border-radius:5px;margin-bottom:20px}";
  html += ".status-item{display:flex;justify-content:space-between;padding:5px 0}";
  html += ".tally-off{color:#888}.tally-green{color:#0f0}.tally-red{color:#f00}.tally-yellow{color:#ff0}";
  html += ".note{font-size:12px;color:#888;margin-top:5px}";
  html += ".conn-eth{color:#4CAF50}.conn-wifi{color:#2196F3}.conn-ap{color:#FF9800}";
  html += ".device-list{max-height:300px;overflow-y:auto}";
  html += ".device-item{display:flex;align-items:center;padding:10px;background:#0f3460;border-radius:5px;margin-bottom:8px}";
  html += ".device-status{width:12px;height:12px;border-radius:50%;margin-right:10px;flex-shrink:0}";
  html += ".device-status.off{background:#666}.device-status.green{background:#0f0}.device-status.red{background:#f00}.device-status.yellow{background:#ff0}";
  html += ".device-info{flex:1;min-width:0}";
  html += ".device-name{font-weight:bold;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
  html += ".device-details{font-size:12px;color:#888}";
  html += ".device-link{padding:8px 12px;background:#00d4ff;color:#1a1a2e;text-decoration:none;border-radius:4px;font-size:12px;white-space:nowrap}";
  html += ".device-link:hover{background:#00b4d8}";
  html += ".refresh-btn{background:#0f3460;padding:8px 15px;margin-bottom:15px}";
  html += ".refresh-btn:hover{background:#1a4a7a}";
  html += ".bulk-btns{display:flex;gap:8px;margin-top:15px}";
  html += ".bulk-btn{flex:1;padding:10px;font-size:12px;margin-top:0}";
  html += ".no-devices{text-align:center;color:#666;padding:20px}";
  html += ".disco-overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.9);z-index:9999;justify-content:center;align-items:center;flex-direction:column}";
  html += ".disco-overlay.active{display:flex}";
  html += ".disco-text{font-size:48px;font-weight:bold;text-align:center;animation:disco-rainbow 0.5s linear infinite}";
  html += "@keyframes disco-rainbow{0%{color:#f00}16%{color:#ff0}33%{color:#0f0}50%{color:#0ff}66%{color:#00f}83%{color:#f0f}100%{color:#f00}}";
  html += ".disco-cancel{margin-top:40px;padding:20px 40px;font-size:20px;background:#c00;border:none;color:#fff;border-radius:10px;cursor:pointer}";
  html += ".disco-cancel:hover{background:#f00}";
  html += "</style></head><body><div class=\"container\">";
  html += "<h1>TSL Tally Configuration</h1>";

  // Status section
  html += "<div class=\"status\">";
  html += "<div class=\"status-item\"><span>Connection:</span><span>" + getConnectionStatus() + "</span></div>";
  html += "<div class=\"status-item\"><span>IP Address:</span><span id=\"currentIP\">" + getActiveIP() + "</span></div>";
  html += "<div class=\"status-item\"><span>Tally State:</span><span id=\"tallyState\" class=\"tally-" + currentTallyState + "\">" + currentTallyState + "</span></div>";
  html += "<div class=\"status-item\"><span>TSL Text:</span><span id=\"tallyText\">" + (currentTallyText.length() > 0 ? currentTallyText : "-") + "</span></div>";
  if (eth_connected) {
    html += "<div class=\"status-item\"><span>ETH MAC:</span><span>" + ETH.macAddress() + "</span></div>";
  }
  if (wifi_connected || ap_mode) {
    html += "<div class=\"status-item\"><span>WiFi MAC:</span><span>" + WiFi.macAddress() + "</span></div>";
  }
  if (ap_mode) {
    html += "<div class=\"status-item\"><span>AP SSID:</span><span>" + apSSID + "</span></div>";
  }
  html += "<div class=\"status-item\"><span>Firmware:</span><span id=\"fwVersion\">" + String(FIRMWARE_VERSION) + "</span>";
  html += "<button type=\"button\" onclick=\"checkUpdate()\" style=\"width:auto;margin-left:10px;margin-top:0;padding:4px 12px;font-size:11px;cursor:pointer\">Check</button></div>";
  html += "<div class=\"status-item\" id=\"updateNotice\" style=\"display:none\"><span style=\"color:#ff6b6b\">Update Available:</span>";
  html += "<span id=\"latestVersion\" style=\"color:#ff6b6b\"></span>";
  html += "<button type=\"button\" onclick=\"installUpdate()\" style=\"margin-left:10px;padding:2px 8px;font-size:12px;background:#4CAF50;color:white;border:none;border-radius:3px;cursor:pointer\">Install</button></div>";
  html += "</div>";

  // Test Tally buttons (momentary - on while pressed)
  html += "<div class=\"card\"><h2>Test Tally</h2>";
  html += "<p class=\"note\">Hold button to test - releases to off</p>";
  html += "<div class=\"test-btns\">";
  html += "<button type=\"button\" class=\"test-btn btn-green\" onmousedown=\"testOn(1)\" onmouseup=\"testOff()\" ontouchstart=\"testOn(1)\" ontouchend=\"testOff()\">GREEN</button>";
  html += "<button type=\"button\" class=\"test-btn btn-red\" onmousedown=\"testOn(2)\" onmouseup=\"testOff()\" ontouchstart=\"testOn(2)\" ontouchend=\"testOff()\">RED</button>";
  html += "<button type=\"button\" class=\"test-btn btn-yellow\" onmousedown=\"testOn(3)\" onmouseup=\"testOff()\" ontouchstart=\"testOn(3)\" ontouchend=\"testOff()\">YELLOW</button>";
  html += "</div></div>";

  // Network Devices section
  html += "<div class=\"card\"><h2>Network Devices</h2>";
  html += "<button type=\"button\" class=\"refresh-btn\" onclick=\"discoverDevices()\">Scan Network</button>";
  html += "<div id=\"deviceList\" class=\"device-list\"><p class=\"no-devices\">Click Scan to find devices</p></div>";
  html += "<div class=\"bulk-btns\">";
  html += "<button type=\"button\" class=\"bulk-btn btn-green\" onclick=\"bulkTest(1)\">All GREEN</button>";
  html += "<button type=\"button\" class=\"bulk-btn btn-red\" onclick=\"bulkTest(2)\">All RED</button>";
  html += "<button type=\"button\" class=\"bulk-btn\" onclick=\"bulkTest(0)\" style=\"background:#333;color:#fff\">All OFF</button>";
  html += "</div>";
  html += "<p class=\"note\">Discovers other TSL tally lights on the network via mDNS</p>";
  html += "</div>";

  // Form
  html += "<form action=\"/save\" method=\"POST\">";

  // TSL Settings
  html += "<div class=\"card\"><h2>TSL Settings</h2>";
  html += "<label for=\"tslAddr\">TSL Address (0-126)</label>";
  html += "<input type=\"number\" id=\"tslAddr\" name=\"tslAddr\" min=\"0\" max=\"126\" value=\"" + String(tslAddress) + "\" required>";
  html += "<label for=\"tslMcast\">Multicast Address</label>";
  html += "<input type=\"text\" id=\"tslMcast\" name=\"tslMcast\" value=\"" + tslMulticast + "\" required>";
  html += "<label for=\"tslPort\">TSL Port</label>";
  html += "<input type=\"number\" id=\"tslPort\" name=\"tslPort\" min=\"1\" max=\"65535\" value=\"" + String(tslPort) + "\" required>";
  html += "<label for=\"maxBright\">Max Brightness (1-255)</label>";
  html += "<input type=\"number\" id=\"maxBright\" name=\"maxBright\" min=\"1\" max=\"255\" value=\"" + String(maxBrightness) + "\" required>";
  html += "<p class=\"note\">TSL brightness (0-3) maps to 0 - max brightness</p>";
  html += "</div>";

  // WiFi Settings
  html += "<div class=\"card\"><h2>WiFi Settings</h2>";
  html += "<label for=\"wifiEn\">WiFi</label>";
  html += "<select id=\"wifiEn\" name=\"wifiEn\" onchange=\"toggleWifiFields()\">";
  html += "<option value=\"0\"" + String(!wifiEnabled ? " selected" : "") + ">Disabled</option>";
  html += "<option value=\"1\"" + String(wifiEnabled ? " selected" : "") + ">Enabled</option>";
  html += "</select>";

  html += "<div id=\"wifiFields\" class=\"wifi-fields\">";
  html += "<label for=\"wifiSSID\">WiFi SSID</label>";
  html += "<input type=\"text\" id=\"wifiSSID\" name=\"wifiSSID\" value=\"" + wifiSSID + "\" maxlength=\"32\">";
  html += "<label for=\"wifiPass\">WiFi Password</label>";
  html += "<input type=\"password\" id=\"wifiPass\" name=\"wifiPass\" value=\"" + wifiPassword + "\" maxlength=\"64\">";
  html += "</div>";
  html += "<p class=\"note\">If WiFi fails, device will start an AP: " + apSSID + " (password: " + apPassword + ")</p>";
  html += "</div>";

  // Ethernet/Network Settings
  html += "<div class=\"card\"><h2>Ethernet Settings</h2>";
  html += "<label for=\"hostname\">Hostname</label>";
  html += "<input type=\"text\" id=\"hostname\" name=\"hostname\" value=\"" + deviceHostname + "\" maxlength=\"32\" required>";

  html += "<label for=\"dhcp\">IP Configuration</label>";
  html += "<select id=\"dhcp\" name=\"dhcp\" onchange=\"toggleIPFields()\">";
  html += "<option value=\"1\"" + String(useDHCP ? " selected" : "") + ">DHCP (Automatic)</option>";
  html += "<option value=\"0\"" + String(!useDHCP ? " selected" : "") + ">Static IP</option>";
  html += "</select>";

  html += "<div id=\"ipFields\" class=\"ip-fields\">";
  html += "<label for=\"ip\">IP Address</label>";
  html += "<input type=\"text\" id=\"ip\" name=\"ip\" value=\"" + staticIP + "\">";
  html += "<label for=\"gw\">Gateway</label>";
  html += "<input type=\"text\" id=\"gw\" name=\"gw\" value=\"" + gateway + "\">";
  html += "<label for=\"sn\">Subnet Mask</label>";
  html += "<input type=\"text\" id=\"sn\" name=\"sn\" value=\"" + subnet + "\">";
  html += "<label for=\"dns\">DNS Server</label>";
  html += "<input type=\"text\" id=\"dns\" name=\"dns\" value=\"" + dns + "\">";
  html += "</div>";
  html += "<p class=\"note\">Device will reboot after saving settings.</p>";
  html += "</div>";

  html += "<div style=\"display:flex;gap:10px;margin-top:20px\">";
  html += "<button type=\"submit\" style=\"flex:2\">Save &amp; Reboot</button>";
  html += "<button type=\"button\" style=\"flex:1;background:#c00\" onclick=\"resetDefaults()\">Reset Defaults</button>";
  html += "</div>";
  html += "</form>";
  html += "<footer style=\"text-align:center;margin-top:30px;padding:20px;color:#666;font-size:12px\">";
  html += "&copy; 2025 <a href=\"https://videowalrus.com\" style=\"color:#00d4ff\">Video Walrus</a>";
  html += "</footer>";
  html += "</div>";

  // Disco mode overlay
  html += "<div id=\"discoOverlay\" class=\"disco-overlay\">";
  html += "<div class=\"disco-text\">DISCO MODE<br>ACTIVATED</div>";
  html += "<button class=\"disco-cancel\" onclick=\"stopDisco()\">STOP THE PARTY</button>";
  html += "</div>";

  // JavaScript
  html += "<script>";
  html += "function toggleIPFields(){var d=document.getElementById('dhcp').value;var f=document.getElementById('ipFields');if(d==='0'){f.classList.add('show')}else{f.classList.remove('show')}}";
  html += "function toggleWifiFields(){var w=document.getElementById('wifiEn').value;var f=document.getElementById('wifiFields');if(w==='1'){f.classList.add('show')}else{f.classList.remove('show')}}";
  html += "function testOn(s){fetch('/test?state='+s).then(r=>r.json()).then(d=>{document.getElementById('tallyState').textContent=d.tally;document.getElementById('tallyState').className='tally-'+d.tally.toLowerCase()})}";
  html += "function testOff(){fetch('/test?state=0').then(r=>r.json()).then(d=>{document.getElementById('tallyState').textContent=d.tally;document.getElementById('tallyState').className='tally-'+d.tally.toLowerCase()})}";
  html += "var devices=[];";
  html += "function discoverDevices(){";
  html += "document.getElementById('deviceList').innerHTML='<p class=\"no-devices\">Scanning...</p>';";
  html += "fetch('/discover').then(r=>r.json()).then(d=>{";
  html += "devices=d.devices;var html='';";
  html += "if(devices.length===0){html='<p class=\"no-devices\">No other devices found</p>';}";
  html += "else{devices.forEach(function(dev){";
  html += "html+='<div class=\"device-item\">';";
  html += "html+='<div class=\"device-status off\" id=\"status-'+dev.ip.replace(/\\./g,'-')+'\"></div>';";
  html += "html+='<div class=\"device-info\">';";
  html += "html+='<div class=\"device-name\">'+dev.hostname+'</div>';";
  html += "html+='<div class=\"device-details\">TSL:'+dev.tslAddress+' | '+dev.ip+'</div>';";
  html += "html+='</div>';";
  html += "html+='<a href=\"http://'+dev.ip+'/\" target=\"_blank\" class=\"device-link\">Open</a>';";
  html += "html+='</div>';";
  html += "});}";
  html += "document.getElementById('deviceList').innerHTML=html;";
  html += "updateDeviceStatuses();";
  html += "}).catch(function(e){document.getElementById('deviceList').innerHTML='<p class=\"no-devices\">Scan failed</p>';});}";
  html += "function updateDeviceStatuses(){";
  html += "devices.forEach(function(dev){";
  html += "fetch('http://'+dev.ip+'/status').then(r=>r.json()).then(d=>{";
  html += "var el=document.getElementById('status-'+dev.ip.replace(/\\./g,'-'));";
  html += "if(el){el.className='device-status '+d.tally.toLowerCase();}";
  html += "}).catch(function(){});});}";
  html += "function bulkTest(state){";
  html += "devices.forEach(function(dev){fetch('http://'+dev.ip+'/test?state='+state).catch(function(){});});";
  html += "fetch('/test?state='+state);}";
  html += "function resetDefaults(){if(confirm('Reset all settings to factory defaults?\\n\\nThis will erase all configuration and reboot the device.')){window.location.href='/reset';}}";
  // Firmware update functions
  html += "function checkUpdate(){";
  html += "document.getElementById('updateNotice').style.display='none';";
  html += "fetch('/api/check-update').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('fwVersion').textContent=d.current;";
  html += "if(d.updateAvailable){";
  html += "document.getElementById('updateNotice').style.display='block';";
  html += "document.getElementById('latestVersion').textContent=d.latest;";
  html += "}else{alert('Firmware is up to date ('+d.current+')');}";
  html += "}).catch(function(e){alert('Failed to check for updates');});}";
  html += "function installUpdate(){";
  html += "if(confirm('Install firmware update?\\n\\nThe device will download the new firmware and reboot.')){";
  html += "document.getElementById('updateNotice').innerHTML='<span style=\\\"color:#ff6b6b\\\">Updating... Please wait, device will reboot</span>';";
  html += "fetch('/api/update').catch(function(){});}}";
  // Secret disco mode - type 'disco' anywhere to trigger
  html += "var discoBuffer='';var discoTimer=null;";
  html += "document.addEventListener('keydown',function(e){";
  html += "discoBuffer+=e.key.toLowerCase();discoBuffer=discoBuffer.slice(-5);";
  html += "if(discoBuffer==='disco'){startDisco();}});";
  html += "function startDisco(){";
  html += "document.getElementById('discoOverlay').classList.add('active');";  // Show overlay
  html += "fetch('/disco?duration=10');";  // Trigger local device immediately
  // If devices already discovered, use them; otherwise scan first
  html += "if(devices.length>0){";
  html += "devices.forEach(function(dev){fetch('http://'+dev.ip+'/disco?duration=10').catch(function(){});});";
  html += "}else{";
  html += "fetch('/discover').then(r=>r.json()).then(d=>{";
  html += "devices=d.devices;";
  html += "devices.forEach(function(dev){fetch('http://'+dev.ip+'/disco?duration=10').catch(function(){});});";
  html += "}).catch(function(){});}";
  html += "discoTimer=setTimeout(function(){document.getElementById('discoOverlay').classList.remove('active');},10000);";  // Auto-hide after 10s
  html += "console.log('DISCO MODE!');}";
  html += "function stopDisco(){";
  html += "if(discoTimer){clearTimeout(discoTimer);}";
  html += "document.getElementById('discoOverlay').classList.remove('active');";  // Hide overlay
  html += "fetch('/disco-stop');";  // Stop local device
  html += "devices.forEach(function(dev){fetch('http://'+dev.ip+'/disco-stop').catch(function(){});});";  // Stop all discovered devices
  html += "}";
  html += "toggleIPFields();toggleWifiFields();";
  html += "discoverDevices();";  // Auto-discover devices on page load
  html += "setInterval(function(){fetch('/status').then(r=>r.json()).then(d=>{document.getElementById('tallyState').textContent=d.tally;document.getElementById('tallyState').className='tally-'+d.tally.toLowerCase();document.getElementById('tallyText').textContent=d.text||'-'});updateDeviceStatuses();},2000);";
  html += "</script></body></html>";

  return html;
}

// Setup web server routes
void setupWebServer() {
  // Main configuration page
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getConfigPage());
  });

  // Status endpoint (JSON)
  server.on("/status", HTTP_GET, []() {
    String json = "{\"tally\":\"" + currentTallyState + "\",\"text\":\"" + currentTallyText + "\",\"ip\":\"" + getActiveIP() + "\",\"connection\":\"" + getConnectionStatus() + "\"}";
    server.send(200, "application/json", json);
  });

  // Test tally endpoint
  server.on("/test", HTTP_GET, []() {
    if (server.hasArg("state")) {
      int state = server.arg("state").toInt();
      setTallyState(state);
    }
    String json = "{\"tally\":\"" + currentTallyState + "\"}";
    server.send(200, "application/json", json);
  });

  // Device info endpoint (for multi-device discovery)
  server.on("/info", HTTP_GET, []() {
    String mac = eth_connected ? ETH.macAddress() : WiFi.macAddress();
    String json = "{";
    json += "\"hostname\":\"" + deviceHostname + "\",";
    json += "\"ip\":\"" + getActiveIP() + "\",";
    json += "\"mac\":\"" + mac + "\",";
    json += "\"tslAddress\":" + String(tslAddress) + ",";
    json += "\"tallyState\":\"" + currentTallyState + "\",";
    json += "\"tallyText\":\"" + currentTallyText + "\",";
    json += "\"connection\":\"" + getConnectionStatus() + "\",";
    json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // Discover other tally devices on the network
  server.on("/discover", HTTP_GET, []() {
    // Only rescan if cache is stale (> 10 seconds)
    if (millis() - lastDiscoveryScan > 10000) {
      discoverTallyDevices();
    }

    // Build JSON array of discovered devices
    String json = "{\"devices\":[";
    for (int i = 0; i < numDiscoveredDevices; i++) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"hostname\":\"" + discoveredDevices[i].hostname + "\",";
      json += "\"ip\":\"" + discoveredDevices[i].ip + "\",";
      json += "\"tslAddress\":" + String(discoveredDevices[i].tslAddress);
      json += "}";
    }
    json += "],\"count\":" + String(numDiscoveredDevices) + "}";
    server.send(200, "application/json", json);
  });

  // Reset to factory defaults
  server.on("/reset", HTTP_GET, []() {
    resetSettings();

    String response = "<!DOCTYPE html><html><head>";
    response += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    response += "<title>Factory Reset</title>";
    response += "<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}.message{text-align:center}h1{color:#c00}</style>";
    response += "</head><body><div class=\"message\"><h1>Factory Reset Complete</h1>";
    response += "<p>All settings have been reset to defaults.</p>";
    response += "<p>Device is rebooting...</p>";
    response += "</div></body></html>";

    server.send(200, "text/html", response);

    delay(1000);
    ESP.restart();
  });

  // Check for firmware updates
  server.on("/api/check-update", HTTP_GET, []() {
    checkForUpdates();
    String json = "{\"current\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"latest\":\"" + latestVersion + "\",";
    json += "\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
    json += "\"firmwareURL\":\"" + firmwareURL + "\"}";
    server.send(200, "application/json", json);
  });

  // Perform firmware update from GitHub
  server.on("/api/update", HTTP_GET, []() {
    if (!updateAvailable || firmwareURL.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"No update available\"}");
      return;
    }
    server.send(200, "application/json", "{\"status\":\"starting\",\"message\":\"Downloading update...\"}");
    delay(100);  // Give time for response to send
    performOTAUpdate();
  });

  // Secret disco mode endpoint
  server.on("/disco", HTTP_GET, []() {
    int duration = 10;  // Default 10 seconds
    if (server.hasArg("duration")) {
      duration = constrain(server.arg("duration").toInt(), 1, 60);
    }
    discoMode = true;
    discoEndTime = millis() + (duration * 1000);
    Serial.printf("[DISCO] Party mode activated for %d seconds!\n", duration);
    server.send(200, "application/json", "{\"disco\":true,\"duration\":" + String(duration) + "}");
  });

  // Stop disco mode
  server.on("/disco-stop", HTTP_GET, []() {
    discoMode = false;
    Serial.println("[DISCO] Party stopped by request!");
    // Return to current tally state
    setTallyState(currentTallyState == "Green" ? 1 :
                  currentTallyState == "Red" ? 2 :
                  currentTallyState == "Yellow" ? 3 : 0);
    server.send(200, "application/json", "{\"disco\":false}");
  });

  // Save settings
  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("tslAddr")) {
      tslAddress = server.arg("tslAddr").toInt();
    }
    if (server.hasArg("tslMcast")) {
      tslMulticast = server.arg("tslMcast");
    }
    if (server.hasArg("tslPort")) {
      tslPort = constrain(server.arg("tslPort").toInt(), 1, 65535);
    }
    if (server.hasArg("maxBright")) {
      maxBrightness = constrain(server.arg("maxBright").toInt(), 1, 255);
    }
    if (server.hasArg("hostname")) {
      deviceHostname = server.arg("hostname");
    }
    if (server.hasArg("dhcp")) {
      useDHCP = server.arg("dhcp") == "1";
    }
    if (server.hasArg("ip")) {
      staticIP = server.arg("ip");
    }
    if (server.hasArg("gw")) {
      gateway = server.arg("gw");
    }
    if (server.hasArg("sn")) {
      subnet = server.arg("sn");
    }
    if (server.hasArg("dns")) {
      dns = server.arg("dns");
    }

    // WiFi settings
    if (server.hasArg("wifiEn")) {
      wifiEnabled = server.arg("wifiEn") == "1";
    }
    if (server.hasArg("wifiSSID")) {
      wifiSSID = server.arg("wifiSSID");
    }
    if (server.hasArg("wifiPass")) {
      wifiPassword = server.arg("wifiPass");
    }

    saveSettings();

    // Build the new address link
    String newAddress = "http://" + deviceHostname + ".local/";
    String ipAddress = useDHCP ? "(DHCP - check router)" : staticIP;

    String response = "<!DOCTYPE html><html><head>";
    response += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    response += "<title>Settings Saved</title>";
    response += "<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}.message{text-align:center}h1{color:#00d4ff}a{color:#00d4ff}</style>";
    response += "</head><body><div class=\"message\"><h1>Settings Saved!</h1>";
    response += "<p>Device is rebooting...</p>";
    response += "<p>Reconnect at: <a href=\"" + newAddress + "\">" + newAddress + "</a></p>";
    if (!useDHCP) {
      response += "<p>Or: <a href=\"http://" + staticIP + "/\">http://" + staticIP + "/</a></p>";
    }
    response += "</div></body></html>";

    server.send(200, "text/html", response);

    // Reboot after a short delay to allow response to be sent
    delay(1000);
    ESP.restart();
  });

  // Captive portal detection endpoints - respond with redirect to trigger popup
  // Android
  server.on("/generate_204", HTTP_GET, []() {
    server.sendHeader("Location", "http://" + getActiveIP() + "/");
    server.send(302, "text/plain", "");
  });
  // Windows
  server.on("/ncsi.txt", HTTP_GET, []() {
    server.sendHeader("Location", "http://" + getActiveIP() + "/");
    server.send(302, "text/plain", "");
  });
  server.on("/connecttest.txt", HTTP_GET, []() {
    server.sendHeader("Location", "http://" + getActiveIP() + "/");
    server.send(302, "text/plain", "");
  });
  // Apple
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.sendHeader("Location", "http://" + getActiveIP() + "/");
    server.send(302, "text/plain", "");
  });
  server.on("/library/test/success.html", HTTP_GET, []() {
    server.sendHeader("Location", "http://" + getActiveIP() + "/");
    server.send(302, "text/plain", "");
  });

  // Catch-all handler for captive portal (redirect unknown requests to config page)
  server.onNotFound([]() {
    if (ap_mode) {
      server.sendHeader("Location", "http://" + getActiveIP() + "/");
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });
}

void setup() {
  Serial.begin(115200);
  while (millis() < 3000);
  Serial.println("Video Walrus Single TSL tally interface 2025");
  Serial.println("");

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is typical
  FastLED.setBrightness(maxBrightness);
  FastLED.clear();  // clear all pixel data
  FastLED.show();

  // Check for factory reset (hold BOOT button for 3 seconds)
  checkResetButton();

  // Load settings from NVS
  loadSettings();

  Network.onEvent(onEvent);

  // Configure static IP if not using DHCP (for Ethernet)
  if (!useDHCP) {
    IPAddress ip, gw, sn, dnsServer;
    ip.fromString(staticIP);
    gw.fromString(gateway);
    sn.fromString(subnet);
    dnsServer.fromString(dns);
    ETH.config(ip, gw, sn, dnsServer);
    Serial.println("Using static IP configuration for Ethernet");
  }

  // Try Ethernet first
  Serial.println("Starting Ethernet...");

  // Hardware reset the W5500
  pinMode(ETH_PHY_RST, OUTPUT);
  digitalWrite(ETH_PHY_RST, LOW);
  delay(50);
  digitalWrite(ETH_PHY_RST, HIGH);
  delay(50);

  // Initialize W5500 - uses ETH_PHY_* defines set before ETH.h include
  bool ethStarted = ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST,
                               ETH_PHY_SPI_HOST, ETH_PHY_SPI_SCK, ETH_PHY_SPI_MISO, ETH_PHY_SPI_MOSI);
  Serial.printf("ETH.begin() returned: %s\n", ethStarted ? "true" : "false");

  if (ethStarted) {
    // Wait for Ethernet with timeout, feeding watchdog
    Serial.println("Waiting for Ethernet connection...");
    unsigned long ethStartTime = millis();
    while (!eth_connected && millis() - ethStartTime < 10000) {  // 10 second timeout
      yield();  // Feed the watchdog
      delay(100);
      // Blink green while waiting for Ethernet
      fill_solid(leds, NUM_LEDS, ((millis() / 300) % 2) ? CRGB::Green : CRGB::Black);
      FastLED.show();
    }
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    Serial.printf("Ethernet wait complete. Connected: %s\n", eth_connected ? "YES" : "NO");
  }

  // If Ethernet connected, use it exclusively
  if (eth_connected) {
    Serial.println("Ethernet connected - using wired network");
  } else {
    // No Ethernet - try WiFi, then AP mode as fallback
    Serial.println("Ethernet not connected, trying WiFi...");

    if (!setupWiFi()) {
      Serial.println("WiFi failed, starting AP mode for configuration...");
      startAP();
    }
  }

  // Wait for network stack after AP mode
  if (ap_mode) {
    delay(2000);  // Give TCP/IP stack time to be ready
  }

  // Setup UDP multicast listener if we have any network connection
  if (eth_connected || wifi_connected) {
    // Parse multicast address from string
    multicastAddress.fromString(tslMulticast);
    Serial.printf("TSL Multicast: %s:%d\n", multicastAddress.toString().c_str(), tslPort);

    // Start UDP listener task on core 0 (main loop runs on core 1)
    startUDPTask();

    // Start mDNS responder
    startMDNS();

    // Run LED test to indicate successful network connection
    testLED();
  } else {
    Serial.println("No network connection for TSL - AP mode only for configuration");
  }

  // Only setup OTA if we have a real network connection (not AP-only mode)
  if (eth_connected || wifi_connected) {
    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else {
          type = "filesystem";
        }
        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

    ArduinoOTA.setHostname(deviceHostname.c_str());
    ArduinoOTA.setPassword("password");
    ArduinoOTA.begin();
    Serial.println("OTA enabled");
  } else {
    Serial.println("OTA disabled (AP mode only)");
  }

  // Setup web server
  setupWebServer();
  server.begin();
  Serial.println("Web server started at http://" + getActiveIP());
}

void loop() {
  // Handle DNS requests for captive portal (AP mode only)
  if (ap_mode) {
    dnsServer.processNextRequest();
  }

  // Handle disco mode animation
  if (discoMode) {
    if (millis() < discoEndTime) {
      // Random rainbow color changes - all LEDs same color, distinct hues
      static unsigned long lastColorChange = 0;
      if (millis() - lastColorChange > 250) {  // Change every 250ms
        lastColorChange = millis();
        // Pick a random hue from 6 distinct rainbow colors (avoid in-between muddy colors)
        uint8_t hueOptions[] = {0, 32, 64, 96, 160, 192};  // Red, Orange, Yellow, Green, Blue, Purple
        uint8_t randomHue = hueOptions[random(6)];
        FastLED.setBrightness(255);  // Full brightness for disco
        fill_solid(leds, NUM_LEDS, CHSV(randomHue, 255, 255));
        FastLED.show();
      }
    } else {
      // Disco time is over
      discoMode = false;
      Serial.println("[DISCO] Party's over!");
      // Return to current tally state
      setTallyState(currentTallyState == "Green" ? 1 :
                    currentTallyState == "Red" ? 2 :
                    currentTallyState == "Yellow" ? 3 : 0);
    }
  }

  // Handle web server requests
  server.handleClient();

  // Handle OTA updates
  ArduinoOTA.handle();

  // Periodic background device discovery (every 60 seconds)
  static unsigned long lastAutoDiscovery = 0;
  if ((eth_connected || wifi_connected) && !ap_mode) {
    if (millis() - lastAutoDiscovery > 60000) {
      lastAutoDiscovery = millis();
      discoverTallyDevices();
    }
  }

  delay(10);
}
