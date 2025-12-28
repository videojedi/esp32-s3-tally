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

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.
// W5500 is SPI-based, doesn't need external clock - use GPIO_IN to avoid conflicts
#define ETH_PHY_ADDR 1
#define ETH_PHY_POWER   5
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_CLK_MODE ETH_CLOCK_GPIO_IN  // Changed from GPIO0_OUT to avoid WiFi conflict
#define ETH_PHY_TYPE ETH_PHY_W5500

#include <ETH.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>

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
void udpListenerTask(void *pvParameters);
void startUDPTask();
void stopUDPTask();

// Web server
WebServer server(80);
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
String currentTallyState = "Off";
String currentTallyText = "";

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
  deviceHostname = preferences.getString("hostname", "ESP-TSL-Tally");
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
  deviceHostname = "ESP-TSL-Tally";
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
  WiFi.setHostname(deviceHostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
    delay(500);
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
  Serial.printf("Starting AP: %s\n", apSSID.c_str());

  // Flash white briefly to show we're about to start AP
  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
  delay(100);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Disconnect any existing WiFi first
  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_AP);
  delay(100);  // Let mode change settle

  bool apStarted = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
  delay(500);  // Give AP time to fully initialize

  if (apStarted) {
    ap_mode = true;
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("AP started! IP: %s\n", apIP.toString().c_str());

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
    Serial.println(text);

    Bright = message[1] & 0b00110000;
    Bright = Bright >> 4;
    Bright = map(Bright, 0, 3, 0, maxBrightness);
    Serial.println(Bright);
    FastLED.setBrightness(Bright);

    setTallyState(T);
  }
}

// UDP listener task - runs on core 0 for reliable multicast reception
void udpListenerTask(void *pvParameters) {
  Serial.printf("[UDP Task] Running on core %d\n", xPortGetCoreID());

  // Start multicast UDP listener
  if (udp.beginMulticast(multicastAddress, tslPort)) {
    Serial.printf("[UDP Task] Multicast listening on %s:%d\n",
                  multicastAddress.toString().c_str(), tslPort);
    udpRunning = true;
  } else {
    Serial.println("[UDP Task] Failed to start multicast UDP!");
    vTaskDelete(NULL);
    return;
  }

  char buffer[256];

  for (;;) {
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
    udpRunning = false;
    udp.stop();
    Serial.println("[UDP] Task stopped");
  }
}

// HTML page for configuration
String getConfigPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
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
  html += ".btn-off{background:#333;color:#fff}.btn-green{background:#0f0;color:#000}.btn-red{background:#f00;color:#fff}.btn-yellow{background:#ff0;color:#000}";
  html += ".status{background:#0f3460;padding:15px;border-radius:5px;margin-bottom:20px}";
  html += ".status-item{display:flex;justify-content:space-between;padding:5px 0}";
  html += ".tally-off{color:#888}.tally-green{color:#0f0}.tally-red{color:#f00}.tally-yellow{color:#ff0}";
  html += ".note{font-size:12px;color:#888;margin-top:5px}";
  html += ".conn-eth{color:#4CAF50}.conn-wifi{color:#2196F3}.conn-ap{color:#FF9800}";
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
  html += "</div>";

  // Test Tally buttons
  html += "<div class=\"card\"><h2>Test Tally</h2>";
  html += "<div class=\"test-btns\">";
  html += "<button type=\"button\" class=\"test-btn btn-off\" onclick=\"testTally(0)\">OFF</button>";
  html += "<button type=\"button\" class=\"test-btn btn-green\" onclick=\"testTally(1)\">GREEN</button>";
  html += "<button type=\"button\" class=\"test-btn btn-red\" onclick=\"testTally(2)\">RED</button>";
  html += "<button type=\"button\" class=\"test-btn btn-yellow\" onclick=\"testTally(3)\">YELLOW</button>";
  html += "</div></div>";

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

  html += "<button type=\"submit\">Save &amp; Reboot</button>";
  html += "</form></div>";

  // JavaScript
  html += "<script>";
  html += "function toggleIPFields(){var d=document.getElementById('dhcp').value;var f=document.getElementById('ipFields');if(d==='0'){f.classList.add('show')}else{f.classList.remove('show')}}";
  html += "function toggleWifiFields(){var w=document.getElementById('wifiEn').value;var f=document.getElementById('wifiFields');if(w==='1'){f.classList.add('show')}else{f.classList.remove('show')}}";
  html += "function testTally(state){fetch('/test?state='+state).then(r=>r.json()).then(d=>{document.getElementById('tallyState').textContent=d.tally;document.getElementById('tallyState').className='tally-'+d.tally.toLowerCase()})}";
  html += "toggleIPFields();toggleWifiFields();";
  html += "setInterval(function(){fetch('/status').then(r=>r.json()).then(d=>{document.getElementById('tallyState').textContent=d.tally;document.getElementById('tallyState').className='tally-'+d.tally.toLowerCase();document.getElementById('tallyText').textContent=d.text||'-'})},2000);";
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

    String response = "<!DOCTYPE html><html><head>";
    response += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    response += "<title>Settings Saved</title>";
    response += "<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}.message{text-align:center}h1{color:#00d4ff}</style>";
    response += "</head><body><div class=\"message\"><h1>Settings Saved!</h1>";
    response += "<p>Device is rebooting...</p>";
    response += "<p>Please reconnect to the device at the new IP address.</p>";
    response += "</div></body></html>";

    server.send(200, "text/html", response);

    // Reboot after a short delay to allow response to be sent
    delay(1000);
    ESP.restart();
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
  bool ethStarted = ETH.begin();
  Serial.printf("ETH.begin() returned: %s\n", ethStarted ? "true" : "false");

  if (ethStarted) {
    // Wait for Ethernet with timeout
    unsigned long ethStartTime = millis();
    while (!eth_connected && millis() - ethStartTime < 5000) {
      delay(100);
      // Blink green while waiting for Ethernet
      fill_solid(leds, NUM_LEDS, ((millis() / 300) % 2) ? CRGB::Green : CRGB::Black);
      FastLED.show();
    }
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }

  // If Ethernet connected, use it
  if (eth_connected) {
    Serial.println("Ethernet connected!");
    // Also try WiFi if enabled (dual network)
    if (wifiEnabled && wifiSSID.length() > 0) {
      Serial.println("Also connecting to WiFi...");
      setupWiFi();
    }
  } else {
    // No Ethernet - use WiFi or AP mode
    Serial.println("Ethernet not connected, switching to WiFi/AP mode...");

    // Stop Ethernet before using WiFi
    ETH.end();
    delay(100);

    // Try WiFi if configured
    if (!setupWiFi()) {
      // WiFi also failed or not configured, start AP
      Serial.println("Starting AP mode for configuration...");
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
  // Handle web server requests
  server.handleClient();
  delay(10);
}
