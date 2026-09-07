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
#define FIRMWARE_VERSION "1.1.0"
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
#include "webpage.h"

// OTA update manifest published by release.sh (see otaCheck)
#define MANIFEST_URL "https://videowalrus-releases.s3.us-east-1.amazonaws.com/tsl-tally-update.json"
#define OTA_MAX_NOTES 8

// Forward declarations
void loadSettings();
void saveSettings();
void resetSettings();
void checkResetButton();
void onEvent(arduino_event_id_t event);
void setupWebServer();
bool udpTSL(char *data);
void setTallyState(int state, int brightness = -1);  // brightness < 0 = maxBrightness
void renderSpinFrame(CRGB colour, uint8_t brightness);
void spinDelay(CRGB colour, unsigned long ms);
void ledLock();
void ledUnlock();
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
String htmlEscape(const String& s);
String jsonEscape(const String& s);

// Web server
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// Configurable settings (loaded from NVS)
int tslAddress = 0;
int maxBrightness = 50;  // Max brightness (0-255), TSL brightness maps to this
enum LedAnimation { LED_ANIM_SOLID = 0, LED_ANIM_SPIN = 1 };
int ledAnimation = LED_ANIM_SOLID;  // How an active tally is drawn on the ring
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

// The LEDs are written from two cores: the UDP task (core 0) on every TSL packet and
// loop() (core 1) for animation frames. ledMutex serialises brightness + fill + show.
SemaphoreHandle_t ledMutex = NULL;
// Active tally as last set by setTallyState(); loop() keeps the spin animation turning
static CRGB tallyColour = CRGB::Black;
static uint8_t tallyLevel = 0;
static volatile bool spinActive = false;

static bool eth_connected = false;
static bool wifi_connected = false;
static bool ap_mode = false;
static bool discoMode = false;
static unsigned long discoEndTime = 0;
String currentTallyState = "Off";
String currentTallyText = "";
// Last state/brightness received over TSL; restored when a test button is released
volatile int tslState = 0;
volatile int tslBrightness = -1;
volatile int tslBrightRaw = -1;  // 0-3 as received, -1 = nothing received yet
// Packets received for this address, shown on the status page
volatile uint32_t tslPackets = 0;
volatile unsigned long tslLastMs = 0;
volatile uint32_t tslLastFrom = 0;  // IPv4 of the last sender

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

// OTA update state, from the release manifest
String latestVersion = "";
String firmwareURL = "";
String firmwareMD5 = "";
String releaseDate = "";
String otaNotes[OTA_MAX_NOTES];
int otaNoteCount = 0;
bool updateAvailable = false;
volatile bool updateInProgress = false;

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
  ledAnimation = preferences.getInt("ledAnim", LED_ANIM_SOLID);
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
  Serial.printf("  LED Animation: %s\n", ledAnimation == LED_ANIM_SPIN ? "Spin" : "Solid");
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
  preferences.putInt("ledAnim", ledAnimation);
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
  ledAnimation = LED_ANIM_SOLID;
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

// ---- LED ring animation ----
// The seven LEDs are one in the middle and six in a ring around it. The spin animation
// keeps the middle LED on at full colour, holds the ring at a dim base level and sweeps
// a bright head with a fading tail round the six once per SPIN_PERIOD_MS. It draws an
// active tally when LED Animation is set to Spin, and the Ethernet / WiFi / AP stages
// of boot.
#define CENTER_LED 0        // data-order index of the middle LED: 0 (NeoPixel Jewel) or 6
#define RING_LEDS (NUM_LEDS - 1)
#define SPIN_PERIOD_MS 800  // one revolution
#define SPIN_BASE 40        // level (0-255) of LEDs away from the head
#define SPIN_LEAD 128       // ramp-up ahead of the head, in 1/256 LED
#define SPIN_TAIL 512       // fade-out behind the head, in 1/256 LED

void ledLock() {
  if (ledMutex) xSemaphoreTake(ledMutex, portMAX_DELAY);
}

void ledUnlock() {
  if (ledMutex) xSemaphoreGive(ledMutex);
}

// Draw one frame of the spin animation for the current millis(). Callers that can run
// concurrently with the UDP task must hold ledMutex.
void renderSpinFrame(CRGB colour, uint8_t brightness) {
  const int32_t ring = RING_LEDS * 256;  // circumference in 1/256 LED
  int32_t head = ((millis() % SPIN_PERIOD_MS) * ring) / SPIN_PERIOD_MS;
  leds[CENTER_LED] = colour;  // middle LED stays on
  for (int k = 0; k < RING_LEDS; k++) {
    int32_t d = head - k * 256;  // how far the head is past this ring position
    if (d < -ring / 2) d += ring;  // wrap to (-ring/2, ring/2]
    if (d > ring / 2) d -= ring;
    uint8_t level = SPIN_BASE;
    if (d >= 0 && d < SPIN_TAIL) {
      level = 255 - ((255 - SPIN_BASE) * d) / SPIN_TAIL;  // tail behind the head
    } else if (d < 0 && d > -SPIN_LEAD) {
      level = 255 - ((255 - SPIN_BASE) * -d) / SPIN_LEAD;  // ramp in ahead of it
    }
    int i = k < CENTER_LED ? k : k + 1;  // ring position -> data index, skipping the middle
    leds[i] = colour;
    leds[i].nscale8(level);
  }
  FastLED.setBrightness(brightness);
  FastLED.show();
}

// Block for at least ms while spinning the ring in colour. Replaces delay() during the
// boot network stages, which run before the UDP task exists so no lock is needed.
void spinDelay(CRGB colour, unsigned long ms) {
  unsigned long start = millis();
  do {
    renderSpinFrame(colour, maxBrightness);
    delay(20);
  } while (millis() - start < ms);
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
  unsigned long lastDot = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
    yield();  // Feed the watchdog
    // Spin purple while connecting
    renderSpinFrame(CRGB::Purple, maxBrightness);
    delay(20);
    if (millis() - lastDot >= 500) {
      lastDot = millis();
      Serial.print(".");
    }
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
  // Spin cyan while the AP comes up
  spinDelay(CRGB::Cyan, 100);

  // Disconnect any existing WiFi first
  WiFi.disconnect(true);
  spinDelay(CRGB::Cyan, 100);

  // Set AP mode first so MAC address is available
  WiFi.mode(WIFI_AP);
  spinDelay(CRGB::Cyan, 100);  // Let mode change settle

  // Generate unique AP SSID using MAC address (must be after WiFi.mode)
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);  // Use softAP MAC for AP mode
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "Tally-%02X%02X%02X-Setup", mac[3], mac[4], mac[5]);
  apSSID = String(ssid);

  Serial.printf("Starting AP: %s\n", apSSID.c_str());

  bool apStarted = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
  spinDelay(CRGB::Cyan, 500);  // Give AP time to fully initialize

  if (apStarted) {
    ap_mode = true;
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("AP started! IP: %s\n", apIP.toString().c_str());

    // Start DNS server for captive portal (redirect all domains to our IP)
    dnsServer.start(53, "*", apIP);
    Serial.println("Captive portal DNS started");

    // Keep spinning cyan long enough to read as "AP started"
    spinDelay(CRGB::Cyan, 1500);
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

// Set tally state directly (used by both TSL and test buttons).
// brightness: 0-255 to apply a TSL-derived level, or -1 for maxBrightness.
// With LED Animation set to Spin, an active tally draws its first spin frame here and
// loop() keeps it turning; Solid (and Off) fill the ring immediately.
void setTallyState(int state, int brightness) {
  CRGB colour;
  switch (state) {
    case 1:  colour = CRGB::Green;  currentTallyState = "Green";  break;
    case 2:  colour = CRGB::Red;    currentTallyState = "Red";    break;
    case 3:  colour = CRGB::Yellow; currentTallyState = "Yellow"; break;
    case 0:  colour = CRGB::Black;  currentTallyState = "Off";    break;
    default: colour = CRGB::Black;  currentTallyState = "Off";    state = 0;
  }
  Serial.printf("Tally: %s\n", currentTallyState.c_str());

  ledLock();
  tallyColour = colour;
  tallyLevel = brightness < 0 ? maxBrightness : brightness;
  spinActive = (ledAnimation == LED_ANIM_SPIN && state != 0);
  if (updateInProgress) {
    // otaInstall() owns the LEDs; the state is redrawn when it finishes or fails
  } else if (spinActive) {
    renderSpinFrame(tallyColour, tallyLevel);
  } else {
    FastLED.setBrightness(tallyLevel);
    fill_solid(leds, NUM_LEDS, tallyColour);
    FastLED.show();
  }
  ledUnlock();
}

bool udpTSL(char *data) {
  char* message;
  int T;
  int Bright;
  int addr;
  String text;
  message = data;

  addr = message[0] - 128;

  if (tslAddress == addr) {
    // Control byte bits: 0 = tally 1, 1 = tally 2, 2 = tally 3, 3 = tally 4, 4-5 = brightness.
    // Only tally 1/2 drive the light; masking to 4 bits let tally 3/4 push T out of
    // range (4-15), which setTallyState() treats as Off.
    T = message[1] & 0b00000011;

    for (int j = 2; j < 18; j++) {
      char c = message[j];
      if (c == '\0') break;  // Stop at null terminator
      if (c >= 32 && c < 127) text += c;  // Only printable ASCII
    }
    text.trim();  // Remove trailing spaces
    currentTallyText = text;
    Serial.printf("Text: %s\n", text.c_str());

    Bright = (message[1] & 0b00110000) >> 4;
    tslBrightRaw = Bright;
    Bright = map(Bright, 0, 3, 0, maxBrightness);
    Serial.printf("Brightness: %d\n", Bright);

    tslState = T;
    tslBrightness = Bright;
    setTallyState(T, Bright);  // setTallyState applies brightness before show()
    return true;
  }
  return false;
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
          if (udpTSL(buffer)) {
            tslPackets++;
            tslLastMs = millis();
            tslLastFrom = (uint32_t)remote;
          }
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

    // Skip duplicates (same IP already in list)
    bool isDuplicate = false;
    for (int j = 0; j < numDiscoveredDevices; j++) {
      if (discoveredDevices[j].ip == foundIP) {
        Serial.printf("[Discovery] Skipping duplicate: %s\n", foundIP.c_str());
        isDuplicate = true;
        break;
      }
    }
    if (isDuplicate) continue;

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

// ---- OTA updates from the release manifest ----
// release.sh uploads the binary and this manifest to S3:
//   {"version":"1.1.0","url":"https://.../tsl-tally-1.1.0.bin","md5":"...","size":1300000,
//    "release_date":"2026-09-07","notes":["...","..."]}

// True if v2 is newer than v1 ("v" prefix optional)
bool isNewerVersion(const String& v1, const String& v2) {
  String a = v1.startsWith("v") ? v1.substring(1) : v1;
  String b = v2.startsWith("v") ? v2.substring(1) : v2;
  int p1[3] = {0, 0, 0}, p2[3] = {0, 0, 0};
  sscanf(a.c_str(), "%d.%d.%d", &p1[0], &p1[1], &p1[2]);
  sscanf(b.c_str(), "%d.%d.%d", &p2[0], &p2[1], &p2[2]);
  for (int i = 0; i < 3; i++) {
    if (p2[i] > p1[i]) return true;
    if (p2[i] < p1[i]) return false;
  }
  return false;
}

// Value of a string field "key":"value" in the flat manifest (no escapes expected)
static String jsonString(const String& j, const char* key) {
  String k = String("\"") + key + "\":";
  int p = j.indexOf(k);
  if (p < 0) return "";
  p = j.indexOf('"', p + k.length());
  if (p < 0) return "";
  int e = j.indexOf('"', p + 1);
  if (e < 0) return "";
  return j.substring(p + 1, e);
}

// Strings of the "notes" array into otaNotes[], quotes and backslash escapes handled
static void parseNotes(const String& j) {
  otaNoteCount = 0;
  int p = j.indexOf("\"notes\":");
  if (p < 0) return;
  p = j.indexOf('[', p);
  if (p < 0) return;
  int end = j.indexOf(']', p);
  if (end < 0) return;
  int i = p + 1;
  while (i < end && otaNoteCount < OTA_MAX_NOTES) {
    int q = j.indexOf('"', i);
    if (q < 0 || q > end) break;
    String s;
    int k = q + 1;
    while (k < end) {
      char c = j[k];
      if (c == '\\' && k + 1 < end) { s += j[k + 1]; k += 2; continue; }
      if (c == '"') break;
      s += c;
      k++;
    }
    otaNotes[otaNoteCount++] = s;
    i = k + 1;
  }
}

// Fetch the manifest and compare its version with ours
void otaCheck() {
  if (!eth_connected && !wifi_connected) {
    Serial.println("[Update] No network connection");
    return;
  }
  Serial.println("[Update] Fetching release manifest...");

  WiFiClientSecure client;
  client.setInsecure();  // S3 certificate chain is not verified

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  http.begin(client, String(MANIFEST_URL) + "?t=" + String(millis()));  // defeat caches
  http.addHeader("User-Agent", "ESP32-Tally-OTA");

  int code = http.GET();
  Serial.printf("[Update] Manifest response: %d\n", code);

  if (code == 200) {
    String payload = http.getString();
    latestVersion = jsonString(payload, "version");
    firmwareURL   = jsonString(payload, "url");
    firmwareMD5   = jsonString(payload, "md5");
    releaseDate   = jsonString(payload, "release_date");
    parseNotes(payload);
    Serial.printf("[Update] Latest: %s, Current: %s, %d notes\n", latestVersion.c_str(), FIRMWARE_VERSION, otaNoteCount);

    updateAvailable = latestVersion.length() > 0 && firmwareURL.startsWith("https://") &&
                      isNewerVersion(FIRMWARE_VERSION, latestVersion);
    Serial.println(updateAvailable ? "[Update] New version available" : "[Update] Firmware is up to date");
  } else {
    Serial.printf("[Update] Check failed: %d\n", code);
  }
  http.end();
}

// Download the binary from the manifest URL and flash it. The manifest MD5 is checked
// by Update.end() before the new image is accepted.
void otaInstall() {
  if (firmwareURL.length() == 0) {
    Serial.println("[Update] No firmware URL");
    return;
  }
  if (updateInProgress) return;
  updateInProgress = true;  // setTallyState() leaves the LEDs alone until this clears
  Serial.printf("[Update] Downloading %s\n", firmwareURL.c_str());

  ledLock();
  FastLED.setBrightness(maxBrightness);
  fill_solid(leds, NUM_LEDS, CRGB::Purple);  // purple while updating
  FastLED.show();
  ledUnlock();

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(60000);
  http.begin(client, firmwareURL);

  int code = http.GET();
  Serial.printf("[Update] Download response: %d\n", code);

  if (code == 200) {
    int len = http.getSize();
    Serial.printf("[Update] Firmware size: %d bytes\n", len);
    if (len > 0 && Update.begin(len)) {
      if (firmwareMD5.length() == 32) Update.setMD5(firmwareMD5.c_str());
      size_t written = Update.writeStream(client);
      Serial.printf("[Update] Written: %u bytes\n", (unsigned)written);
      if (Update.end() && Update.isFinished()) {
        Serial.println("[Update] Success, rebooting...");
        ledLock();
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        ledUnlock();
        delay(1000);
        ESP.restart();
      } else {
        Serial.printf("[Update] Error: %s\n", Update.errorString());
      }
    } else {
      Serial.printf("[Update] Cannot begin update: %s\n", Update.errorString());
    }
  }
  http.end();
  updateInProgress = false;

  // Failed: show red for two seconds, then back to the current tally
  ledLock();
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  ledUnlock();
  delay(2000);
  setTallyState(tslState, tslBrightness);
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

// Escape a string for safe insertion into HTML text or attribute values
String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&':  out += "&amp;"; break;
      case '<':  out += "&lt;"; break;
      case '>':  out += "&gt;"; break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default:   out += c;
    }
  }
  return out;
}

// Escape a string for safe insertion into a JSON string literal
String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// ---- Web server ----

static String q(const String& s) { return "\"" + jsonEscape(s) + "\""; }
static String b(bool v) { return v ? "true" : "false"; }

static void sendJson(const String& json, bool cors = false) {
  if (cors) server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

static void redirectHome() {
  server.sendHeader("Location", "http://" + getActiveIP() + "/");
  server.send(302, "text/plain", "");
}

// Small themed page for the reboot / reset responses
static String messagePage(const String& title, const String& body, const String& colour) {
  String r = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  r += "<title>" + title + "</title>";
  r += "<meta name=\"color-scheme\" content=\"dark light\"><script>try{var t=localStorage.getItem('theme');if(t==='light'||t==='dark')document.documentElement.setAttribute('data-theme',t)}catch(e){}</script>";
  r += "<style>:root{color-scheme:dark;--bg:#1a1a2e;--fg:#eee;--link:#00d4ff}@media(prefers-color-scheme:light){:root:not([data-theme=dark]){color-scheme:light;--bg:#f2f4f8;--fg:#1a1a2e;--link:#0077a8}}:root[data-theme=light]{color-scheme:light;--bg:#f2f4f8;--fg:#1a1a2e;--link:#0077a8}";
  r += "body{font-family:Arial,sans-serif;background:var(--bg);color:var(--fg);display:flex;justify-content:center;align-items:center;height:100vh;margin:0}.m{text-align:center;padding:20px}h1{color:" + colour + "}a{color:var(--link)}</style>";
  r += "</head><body><div class=\"m\"><h1>" + title + "</h1>" + body + "</div></body></html>";
  return r;
}

static String activeMac() {
  if (eth_connected) return ETH.macAddress();
  if (ap_mode && !wifi_connected) return WiFi.softAPmacAddress();
  return WiFi.macAddress();
}

// Polled every second by the page, and by other tally lights for their device list
static String statusJson() {
  String j = "{\"tally\":" + q(currentTallyState) + ",\"text\":" + q(currentTallyText);
  j += ",\"ip\":" + q(getActiveIP()) + ",\"connection\":" + q(getConnectionStatus());
  j += ",\"pkts\":" + String((unsigned long)tslPackets);
  j += ",\"age\":" + String(tslLastMs ? (unsigned long)(millis() - tslLastMs) : 0UL);
  j += ",\"from\":" + q(IPAddress((uint32_t)tslLastFrom).toString()) + "}";
  return j;
}

// Everything the page needs to fill its form and header
static String configJson() {
  String j = "{";
  j += "\"hostname\":" + q(deviceHostname) + ",";
  j += "\"dhcp\":" + b(useDHCP) + ",";
  j += "\"ip\":" + q(staticIP) + ",\"gw\":" + q(gateway) + ",\"sn\":" + q(subnet) + ",\"dns\":" + q(dns) + ",";
  j += "\"wifiEn\":" + b(wifiEnabled) + ",\"ssid\":" + q(wifiSSID) + ",\"pass\":" + q(wifiPassword) + ",";
  j += "\"tslAddr\":" + String(tslAddress) + ",\"mcast\":" + q(tslMulticast) + ",\"port\":" + String(tslPort) + ",";
  j += "\"maxBright\":" + String(maxBrightness) + ",\"ledAnim\":" + String(ledAnimation) + ",";
  j += "\"apMode\":" + b(ap_mode) + ",\"apSsid\":" + q(ap_mode ? apSSID : String("Tally-XXXXXX-Setup")) + ",\"apPass\":" + q(apPassword) + ",";
  j += "\"mac\":" + q(activeMac()) + ",";
  j += "\"fw\":" + q(FIRMWARE_VERSION) + ",";
  j += "\"build\":\"" __DATE__ " " __TIME__ "\"";
  j += "}";
  return j;
}

// Letters, digits and hyphens only, 32 max; spaces, underscores and dots become hyphens
static String sanitizeHostname(String h) {
  h.trim();
  String out;
  for (size_t i = 0; i < h.length() && out.length() < 32; i++) {
    char c = h[i];
    if (isalnum((unsigned char)c) || c == '-') out += c;
    else if (c == ' ' || c == '_' || c == '.') out += '-';
  }
  while (out.startsWith("-")) out.remove(0, 1);
  while (out.endsWith("-")) out.remove(out.length() - 1);
  if (out.length() == 0) out = getDefaultHostname();
  return out;
}

static bool validIP(const String& s) {
  IPAddress ip;
  return ip.fromString(s);
}

// Save from the web form. Settings that only take effect at boot (network, WiFi,
// hostname, TSL socket) reboot the device; the rest apply at once.
static void handleSave() {
  String bHost = deviceHostname, bIP = staticIP, bGW = gateway, bSN = subnet, bDNS = dns;
  String bSSID = wifiSSID, bPass = wifiPassword, bMcast = tslMulticast;
  bool bDHCP = useDHCP, bWifi = wifiEnabled;
  int bPort = tslPort;

  if (server.hasArg("tslAddr")) tslAddress = constrain(server.arg("tslAddr").toInt(), 0, 126);
  if (server.hasArg("tslMcast")) {
    String m = server.arg("tslMcast");
    m.trim();
    IPAddress ip;
    if (ip.fromString(m) && ip[0] >= 224 && ip[0] <= 239) tslMulticast = m;
  }
  if (server.hasArg("tslPort")) {
    long p = server.arg("tslPort").toInt();
    if (p >= 1 && p <= 65535) tslPort = p;
  }
  if (server.hasArg("maxBright")) maxBrightness = constrain(server.arg("maxBright").toInt(), 1, 255);
  if (server.hasArg("ledAnim")) ledAnimation = server.arg("ledAnim") == "1" ? LED_ANIM_SPIN : LED_ANIM_SOLID;

  if (server.hasArg("hostname")) deviceHostname = sanitizeHostname(server.arg("hostname"));
  if (server.hasArg("dhcp")) useDHCP = server.arg("dhcp") != "0";
  if (server.hasArg("ip") && validIP(server.arg("ip")))   staticIP = server.arg("ip");
  if (server.hasArg("gw") && validIP(server.arg("gw")))   gateway = server.arg("gw");
  if (server.hasArg("sn") && validIP(server.arg("sn")))   subnet = server.arg("sn");
  if (server.hasArg("dns") && validIP(server.arg("dns"))) dns = server.arg("dns");

  if (server.hasArg("wifiEn")) wifiEnabled = server.arg("wifiEn") == "1";
  if (server.hasArg("wifiSSID")) wifiSSID = server.arg("wifiSSID");
  if (server.hasArg("wifiPass")) wifiPassword = server.arg("wifiPass");

  saveSettings();

  bool reboot = bHost != deviceHostname || bIP != staticIP || bGW != gateway || bSN != subnet ||
                bDNS != dns || bSSID != wifiSSID || bPass != wifiPassword || bMcast != tslMulticast ||
                bDHCP != useDHCP || bWifi != wifiEnabled || bPort != tslPort;

  if (!reboot) {
    // Live apply: redraw the current tally with the new brightness / animation and
    // advertise the new address over mDNS
    if (tslBrightRaw >= 0) tslBrightness = map(tslBrightRaw, 0, 3, 0, maxBrightness);
    setTallyState(tslState, tslBrightness);
    if (eth_connected || wifi_connected) MDNS.addServiceTxt("tally", "tcp", "tsladdr", String(tslAddress));
    Serial.println("Settings applied without reboot");
    server.sendHeader("Location", "/?saved=1");
    server.send(302, "text/plain", "");
    return;
  }

  String link = "http://" + deviceHostname + ".local/";
  String body = "<p>Device is rebooting...</p><p>Reconnect at: <a href=\"" + htmlEscape(link) + "\">" + htmlEscape(link) + "</a></p>";
  if (!useDHCP) body += "<p>Or: <a href=\"http://" + htmlEscape(staticIP) + "/\">http://" + htmlEscape(staticIP) + "/</a></p>";
  server.send(200, "text/html", messagePage("Settings Saved", body, "#00d4ff"));
  delay(1000);
  ESP.restart();
}

// Scan for WiFi networks. ?start=1 begins a new async scan (unless one is already
// running); a call without it returns the last result. Enables the STA interface as
// a side effect (AP mode becomes AP+STA, Ethernet-only gains an idle STA); the AP
// stays up, but beacons pause briefly while the radio hops channels.
static void handleWifiScan() {
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED || (server.hasArg("start") && n != WIFI_SCAN_RUNNING)) {
    WiFi.scanDelete();
    n = WiFi.scanNetworks(true);  // async
  }
  if (n == WIFI_SCAN_RUNNING) {
    sendJson("{\"scanning\":true}");
    return;
  }
  if (n < 0) {
    sendJson("{\"scanning\":false,\"error\":\"scan failed\"}");
    return;
  }
  String json = "{\"scanning\":false,\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":" + q(WiFi.SSID(i)) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"ch\":" + String(WiFi.channel(i)) + ",";
    json += "\"enc\":" + b(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    json += "}";
  }
  json += "]}";
  sendJson(json);
}

// Setup web server routes
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE_HTML);
  });

  server.on("/status", HTTP_GET, []() { sendJson(statusJson(), true); });
  server.on("/api/config", HTTP_GET, []() { sendJson(configJson()); });

  // Test tally endpoint - with CORS for cross-device control
  server.on("/test", HTTP_GET, []() {
    if (server.hasArg("restore")) {
      setTallyState(tslState, tslBrightness);  // back to what the switcher last sent
    } else if (server.hasArg("state")) {
      setTallyState(server.arg("state").toInt());
    }
    sendJson("{\"tally\":" + q(currentTallyState) + ",\"text\":" + q(currentTallyText) + "}", true);
  });

  // Device info endpoint (for multi-device discovery) - with CORS
  server.on("/info", HTTP_GET, []() {
    String json = "{";
    json += "\"hostname\":" + q(deviceHostname) + ",";
    json += "\"ip\":" + q(getActiveIP()) + ",";
    json += "\"mac\":" + q(activeMac()) + ",";
    json += "\"tslAddress\":" + String(tslAddress) + ",";
    json += "\"tallyState\":" + q(currentTallyState) + ",";
    json += "\"tallyText\":" + q(currentTallyText) + ",";
    json += "\"connection\":" + q(getConnectionStatus()) + ",";
    json += "\"firmware\":" + q(FIRMWARE_VERSION) + ",";
    json += "\"build\":\"" __DATE__ " " __TIME__ "\"";
    json += "}";
    sendJson(json, true);
  });

  // Discover other tally devices on the network
  server.on("/discover", HTTP_GET, []() {
    // Only rescan if cache is stale (> 10 seconds)
    if (millis() - lastDiscoveryScan > 10000) {
      discoverTallyDevices();
    }
    String json = "{\"devices\":[";
    for (int i = 0; i < numDiscoveredDevices; i++) {
      if (i > 0) json += ",";
      json += "{\"hostname\":" + q(discoveredDevices[i].hostname) + ",";
      json += "\"ip\":" + q(discoveredDevices[i].ip) + ",";
      json += "\"tslAddress\":" + String(discoveredDevices[i].tslAddress) + "}";
    }
    json += "],\"count\":" + String(numDiscoveredDevices) + "}";
    sendJson(json);
  });

  server.on("/api/wifi-scan", HTTP_GET, handleWifiScan);
  server.on("/save", HTTP_POST, handleSave);

  // Reset to factory defaults
  server.on("/reset", HTTP_GET, []() {
    resetSettings();
    server.send(200, "text/html", messagePage("Factory Reset Complete",
                "<p>All settings have been reset to defaults.</p><p>Device is rebooting...</p>", "#c00"));
    delay(1000);
    ESP.restart();
  });

  // Check the release manifest for a newer firmware
  server.on("/api/check-update", HTTP_GET, []() {
    otaCheck();
    String json = "{\"current\":" + q(FIRMWARE_VERSION) + ",";
    json += "\"latest\":" + q(latestVersion) + ",";
    json += "\"updateAvailable\":" + b(updateAvailable) + ",";
    json += "\"firmwareURL\":" + q(firmwareURL) + ",";
    json += "\"date\":" + q(releaseDate) + ",";
    json += "\"notes\":[";
    for (int i = 0; i < otaNoteCount; i++) { if (i) json += ","; json += q(otaNotes[i]); }
    json += "]}";
    sendJson(json);
  });

  // Download and install the firmware from the manifest
  server.on("/api/update", HTTP_GET, []() {
    if (!updateAvailable || firmwareURL.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"No update available\"}");
      return;
    }
    sendJson("{\"status\":\"starting\",\"message\":\"Downloading update...\"}");
    delay(100);  // Give time for response to send
    otaInstall();
  });

  // Secret disco mode endpoint - with CORS for cross-device sync
  server.on("/disco", HTTP_GET, []() {
    int duration = 30;  // Default 30 seconds
    if (server.hasArg("duration")) {
      duration = constrain(server.arg("duration").toInt(), 1, 120);
    }
    discoMode = true;
    discoEndTime = millis() + (duration * 1000);
    Serial.printf("[DISCO] Party mode activated for %d seconds!\n", duration);
    sendJson("{\"disco\":true,\"duration\":" + String(duration) + "}", true);
  });

  // Stop disco mode - with CORS for cross-device sync
  server.on("/disco-stop", HTTP_GET, []() {
    discoMode = false;
    Serial.println("[DISCO] Party stopped by request!");
    setTallyState(tslState, tslBrightness);  // back to what the switcher last sent
    sendJson("{\"disco\":false}", true);
  });

  // Captive portal probes - redirect to the config page to trigger the popup
  server.on("/generate_204", HTTP_GET, redirectHome);              // Android
  server.on("/ncsi.txt", HTTP_GET, redirectHome);                  // Windows
  server.on("/connecttest.txt", HTTP_GET, redirectHome);
  server.on("/hotspot-detect.html", HTTP_GET, redirectHome);       // Apple
  server.on("/library/test/success.html", HTTP_GET, redirectHome);

  // Catch-all handler for captive portal (redirect unknown requests to config page)
  server.onNotFound([]() {
    if (ap_mode) redirectHome();
    else server.send(404, "text/plain", "Not found");
  });
}

void setup() {
  Serial.begin(115200);
  while (millis() < 3000);
  Serial.println("Video Walrus Single TSL tally interface 2025");
  Serial.println("");

  ledMutex = xSemaphoreCreateMutex();
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is typical
  FastLED.setBrightness(maxBrightness);
  FastLED.clear();  // clear all pixel data
  FastLED.show();

  // Check for factory reset (hold BOOT button for 3 seconds)
  checkResetButton();

  // Load settings from NVS
  loadSettings();

  Network.onEvent(onEvent);

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
    // Configure static IP immediately after begin (must be after begin but before DHCP starts)
    if (!useDHCP) {
      IPAddress ip, gw, sn, dnsServer;
      ip.fromString(staticIP);
      gw.fromString(gateway);
      sn.fromString(subnet);
      dnsServer.fromString(dns);
      Serial.println("Configuring static IP...");
      ETH.config(ip, gw, sn, dnsServer);
      Serial.println("Static IP configured");
    }

    // Wait for Ethernet with timeout, feeding watchdog
    Serial.println("Waiting for Ethernet connection...");
    unsigned long ethStartTime = millis();
    while (!eth_connected && millis() - ethStartTime < 10000) {  // 10 second timeout
      yield();  // Feed the watchdog
      delay(20);
      // Spin orange while waiting for Ethernet
      renderSpinFrame(CRGB::Orange, maxBrightness);
    }
    FastLED.setBrightness(maxBrightness);  // Restore brightness
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
        ledLock();
        FastLED.setBrightness(255);  // Full brightness for disco
        fill_solid(leds, NUM_LEDS, CHSV(randomHue, 255, 255));
        FastLED.show();
        ledUnlock();
      }
    } else {
      // Disco time is over
      discoMode = false;
      Serial.println("[DISCO] Party's over!");
      setTallyState(tslState, tslBrightness);  // back to what the switcher last sent
    }
  }

  // Keep the spin animation turning while a tally is active. Disco owns the LEDs
  // while it runs; setTallyState() restarts the spin when it ends.
  static unsigned long lastSpinFrame = 0;
  if (spinActive && !discoMode && millis() - lastSpinFrame >= 25) {
    lastSpinFrame = millis();
    ledLock();
    if (spinActive) renderSpinFrame(tallyColour, tallyLevel);
    ledUnlock();
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
