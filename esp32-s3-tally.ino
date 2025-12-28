/*
    ESP32-S3 TSL Tally Light with Web Configuration
    Video Walrus 2025
*/

#include <FastLED.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

#define BUFFER_LENGTH 256
#define NUM_LEDS 7
#define DATA_PIN 16
#define RESET_BUTTON_PIN 0  // GPIO 0 (BOOT button) for factory reset

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.
#define ETH_PHY_ADDR 1
#define ETH_PHY_POWER   5
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_OUT
#define ETH_PHY_TYPE ETH_PHY_W5500

#include <ETH.h>
#include <NetworkUdp.h>
#include <AsyncUDP.h>
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

// Web server
AsyncWebServer server(80);
Preferences preferences;

int MAXbright = 50;

// Configurable settings (loaded from NVS)
int tslAddress = 0;
bool useDHCP = true;
String staticIP = "192.168.1.100";
String gateway = "192.168.1.1";
String subnet = "255.255.255.0";
String dns = "8.8.8.8";
String deviceHostname = "ESP-TSL-Tally";

bool redTally = false;
int redLED = false;
bool greenTally = false;
int greenLED = false;

IPAddress multicastAddress(239, 1, 2, 3);
unsigned int multicastPort = 8901;

AsyncUDP udp;

CRGB leds[NUM_LEDS];

static bool eth_connected = false;
String currentTallyState = "Off";

// Load settings from NVS
void loadSettings() {
  preferences.begin("tally", true);  // read-only
  tslAddress = preferences.getInt("tslAddress", 0);
  useDHCP = preferences.getBool("useDHCP", true);
  staticIP = preferences.getString("staticIP", "192.168.1.100");
  gateway = preferences.getString("gateway", "192.168.1.1");
  subnet = preferences.getString("subnet", "255.255.255.0");
  dns = preferences.getString("dns", "8.8.8.8");
  deviceHostname = preferences.getString("hostname", "ESP-TSL-Tally");
  preferences.end();

  Serial.println("Settings loaded:");
  Serial.printf("  TSL Address: %d\n", tslAddress);
  Serial.printf("  DHCP: %s\n", useDHCP ? "Yes" : "No");
  if (!useDHCP) {
    Serial.printf("  Static IP: %s\n", staticIP.c_str());
    Serial.printf("  Gateway: %s\n", gateway.c_str());
    Serial.printf("  Subnet: %s\n", subnet.c_str());
    Serial.printf("  DNS: %s\n", dns.c_str());
  }
  Serial.printf("  Hostname: %s\n", deviceHostname.c_str());
}

// Save settings to NVS
void saveSettings() {
  preferences.begin("tally", false);  // read-write
  preferences.putInt("tslAddress", tslAddress);
  preferences.putBool("useDHCP", useDHCP);
  preferences.putString("staticIP", staticIP);
  preferences.putString("gateway", gateway);
  preferences.putString("subnet", subnet);
  preferences.putString("dns", dns);
  preferences.putString("hostname", deviceHostname);
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
  useDHCP = true;
  staticIP = "192.168.1.100";
  gateway = "192.168.1.1";
  subnet = "255.255.255.0";
  dns = "8.8.8.8";
  deviceHostname = "ESP-TSL-Tally";
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
    default: break;
  }
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
    Serial.println(text);

    Bright = message[1] & 0b00110000;
    Bright = Bright >> 4;
    Bright = map(Bright, 0, 3, 0, MAXbright);
    Serial.println(Bright);
    FastLED.setBrightness(Bright);

    switch (T) {
      case 0:
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        currentTallyState = "Off";
        Serial.println("Off");
        break;
      case 1:
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        currentTallyState = "Green";
        Serial.println("Green");
        break;
      case 2:
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        currentTallyState = "Red";
        Serial.println("Red");
        break;
      case 3:
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
        currentTallyState = "Yellow";
        Serial.println("Yellow");
        break;
      default:
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        currentTallyState = "Off";
        Serial.println("Off*");
    }
    FastLED.show();
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
  html += "input[type=text],input[type=number],select{width:100%;padding:10px;border:1px solid #0f3460;border-radius:5px;background:#0f3460;color:#eee;box-sizing:border-box}";
  html += "input:focus,select:focus{outline:none;border-color:#00d4ff}";
  html += ".ip-fields{display:none}.ip-fields.show{display:block}";
  html += "button{width:100%;padding:15px;background:#00d4ff;color:#1a1a2e;border:none;border-radius:5px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:20px}";
  html += "button:hover{background:#00b4d8}";
  html += ".status{background:#0f3460;padding:15px;border-radius:5px;margin-bottom:20px}";
  html += ".status-item{display:flex;justify-content:space-between;padding:5px 0}";
  html += ".tally-off{color:#888}.tally-green{color:#0f0}.tally-red{color:#f00}.tally-yellow{color:#ff0}";
  html += ".note{font-size:12px;color:#888;margin-top:5px}";
  html += "</style></head><body><div class=\"container\">";
  html += "<h1>TSL Tally Configuration</h1>";

  // Status section
  html += "<div class=\"status\">";
  html += "<div class=\"status-item\"><span>IP Address:</span><span id=\"currentIP\">" + ETH.localIP().toString() + "</span></div>";
  html += "<div class=\"status-item\"><span>Tally State:</span><span id=\"tallyState\" class=\"tally-" + currentTallyState + "\">" + currentTallyState + "</span></div>";
  html += "<div class=\"status-item\"><span>MAC:</span><span>" + ETH.macAddress() + "</span></div>";
  html += "</div>";

  // Form
  html += "<form action=\"/save\" method=\"POST\">";

  // TSL Settings
  html += "<div class=\"card\"><h2>TSL Settings</h2>";
  html += "<label for=\"tslAddr\">TSL Address (0-126)</label>";
  html += "<input type=\"number\" id=\"tslAddr\" name=\"tslAddr\" min=\"0\" max=\"126\" value=\"" + String(tslAddress) + "\" required>";
  html += "</div>";

  // Network Settings
  html += "<div class=\"card\"><h2>Network Settings</h2>";
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
  html += "toggleIPFields();";
  html += "setInterval(function(){fetch('/status').then(r=>r.json()).then(d=>{document.getElementById('tallyState').textContent=d.tally;document.getElementById('tallyState').className='tally-'+d.tally.toLowerCase()})},2000);";
  html += "</script></body></html>";

  return html;
}

// Setup web server routes
void setupWebServer() {
  // Main configuration page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getConfigPage());
  });

  // Status endpoint (JSON)
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"tally\":\"" + currentTallyState + "\",\"ip\":\"" + ETH.localIP().toString() + "\"}";
    request->send(200, "application/json", json);
  });

  // Save settings
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("tslAddr", true)) {
      tslAddress = request->getParam("tslAddr", true)->value().toInt();
    }
    if (request->hasParam("hostname", true)) {
      deviceHostname = request->getParam("hostname", true)->value();
    }
    if (request->hasParam("dhcp", true)) {
      useDHCP = request->getParam("dhcp", true)->value() == "1";
    }
    if (request->hasParam("ip", true)) {
      staticIP = request->getParam("ip", true)->value();
    }
    if (request->hasParam("gw", true)) {
      gateway = request->getParam("gw", true)->value();
    }
    if (request->hasParam("sn", true)) {
      subnet = request->getParam("sn", true)->value();
    }
    if (request->hasParam("dns", true)) {
      dns = request->getParam("dns", true)->value();
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

    request->send(200, "text/html", response);

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
  FastLED.setBrightness(MAXbright);
  FastLED.clear();  // clear all pixel data
  FastLED.show();

  // Check for factory reset (hold BOOT button for 3 seconds)
  checkResetButton();

  // Load settings from NVS
  loadSettings();

  Network.onEvent(onEvent);

  // Configure static IP if not using DHCP
  if (!useDHCP) {
    IPAddress ip, gw, sn, dnsServer;
    ip.fromString(staticIP);
    gw.fromString(gateway);
    sn.fromString(subnet);
    dnsServer.fromString(dns);
    ETH.config(ip, gw, sn, dnsServer);
    Serial.println("Using static IP configuration");
  }

  ETH.begin();
  while (!ETH.hasIP());
  if (udp.listenMulticast(multicastAddress, multicastPort)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(ETH.localIP());
    udp.onPacket([](AsyncUDPPacket packet) {
      Serial.print("UDP Packet Type: ");
      Serial.print(packet.isBroadcast() ? "Broadcast" : packet.isMulticast() ? "Multicast" : "Unicast");
      Serial.print(", From: ");
      Serial.print(packet.remoteIP());
      Serial.print(":");
      Serial.print(packet.remotePort());
      Serial.print(", To: ");
      Serial.print(packet.localIP());
      Serial.print(":");
      Serial.print(packet.localPort());
      Serial.print(", Length: ");
      Serial.print(packet.length());
      Serial.println();
      char* msg = (char*)packet.data();
      udpTSL(msg);
    });
  }

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
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
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

  ArduinoOTA.setHostname(deviceHostname.c_str());
  ArduinoOTA.setPassword("password");
  ArduinoOTA.begin();

  // Setup web server
  setupWebServer();
  server.begin();
  Serial.println("Web server started at http://" + ETH.localIP().toString());
}

void loop() {
  ArduinoOTA.handle();
}
