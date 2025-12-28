#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <FastLED.h>
#include <MDNS_Generic.h>



// W5500 SPI pins (adjust if needed)
#define ETH_CS 14
#define ETH_SCLK 13
#define ETH_MISO 12
#define ETH_MOSI 11
#define ETH_RST 9

// Fastled setup
#define BUFFER_LENGTH 256
#define NUM_LEDS 7
#define DATA_PIN 16
CRGB leds[NUM_LEDS];

// TSL setup
int MAXbright = 100;

const int address = 1;  //set TSL tally number here

bool redTally = false;
int redLED = false;
bool greenTally = false;
int greenLED = false;

// Fallback static IP settings
IPAddress staticIP(192, 168, 1, 51);
IPAddress staticGW(192, 168, 1, 1);
IPAddress staticMask(255, 255, 255, 0);
IPAddress staticDNS(8, 8, 8, 8);

// Track connection state
bool ethConnected = false;
bool mdnsStarted = false;
unsigned long lastLinkCheck = 0;

// MAC address (customize if required)
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// UDP configuration
const unsigned int localPort = 8901;  // UDP port to listen on
const bool useMulticast = true;       // set to true for multicast
IPAddress multicastIP(239, 1, 2, 3);  // multicast group if enabled
EthernetUDP Udp;
MDNS mdns(Udp);


TaskHandle_t udpTaskHandle = NULL;
bool udpRunning = false;
bool udpRestart = false;

void udpListenerTask(void *pvParameters) {
  Serial.printf("[UDP Task] Running on core %d\n", xPortGetCoreID());

  for (;;) {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
      IPAddress remote = Udp.remoteIP();
      uint16_t port = Udp.remotePort();

      char buffer[512];
      int len = Udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) buffer[len] = '\0';

      //Serial.printf("[UDP Task] Packet from %s:%d - %s\n", remote.toString().c_str(), port, buffer);
      udpTSL(buffer);
    }
    // Small delay to yield CPU
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void restartUDPTask() {
  if (udpTaskHandle) {
    Serial.println("UDP task killed");
    vTaskDelete(udpTaskHandle);
    udpTaskHandle = NULL;
  }
  xTaskCreatePinnedToCore(udpListenerTask, "UDP Task", 4096, NULL, 1, &udpTaskHandle, 0);
  Serial.println("UDP task restarted");
}

void stopUDPTask() {
  if (udpTaskHandle) {
    Serial.println("UDP task killed");
    vTaskDelete(udpTaskHandle);
    udpTaskHandle = NULL;
  }
}

void setup() {

  Serial.begin(115200);

  while (millis() < 3000);
  Serial.printf("\n--------------------------------------------\n");
  Serial.printf("\n\nVideo Walrus Single TSL tally interface 2025\n\n");
  Serial.printf("\n--------------------------------------------\n");
  Serial.printf("Setup running on core %d\n", xPortGetCoreID());

  // Init Ethernet
  SPI.begin(ETH_SCLK, ETH_MISO, ETH_MOSI, ETH_CS);
  Ethernet.init(ETH_CS);

  tryDHCP();


  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is typical
  FastLED.setBrightness(MAXbright);
  testLED();
  FastLED.clear();  // clear all pixel data
  FastLED.show();



}

void testLED() {
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

void loop() {

  // Your main application runs on core 1 by default
  if (millis() - lastLinkCheck > 1000) {
    lastLinkCheck = millis();
    //Serial.printf("[Main Loop] Running on core %d - Alive %lu\n", xPortGetCoreID(), millis() / 1000);
    checkEthernetLink();
    mdns.run();
  }
  
  }

void udpTSL(char *data) {

  char *message;
  bool T1, T2, T3, T4;
  int T;
  int Bright;
  int addr;
  String text;
  message = data;

  //offset for address 
  addr = message[0] - 128;

  if (address == addr) {
    //TSL v3
    //individual tally bits
    T1 = message[1] & B00000001;
    T2 = (message[1] & B00000010) >>1;
    T3 = (message[1] & B00000100) >>2;
    T4 = (message[1] & B00001000) >>3;

    //Get tally byte
    T = message[1] & B00001111;

    //Get UMD text
    for (int j = 2; j < 17; j++) {
      text += message[j];
    }

    //Get Brightness
    Bright = message[1] & B00110000;

    Bright = Bright >> 4;

    Bright = map(Bright, 0, 3, 0, MAXbright);

    FastLED.setBrightness(Bright);

    String tally;
    switch (T) {
      case 0:
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        tally = "Off";

        break;
      case 1:
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        tally = "Green";

        break;
      case 2:
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        tally = "Red";

        break;
      case 3:
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
        tally = "Yellow";

        break;
      default:
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        tally = "Off*";
    }
    FastLED.show();
    Serial.printf("From: %s Address: %d UMD: %s Brightness: %d Colour: %s\n", Udp.remoteIP().toString().c_str(), addr, text.c_str(), Bright, tally);

  } else {
    // Serial.println("Ignored.");    //message not for me
  }
}


// --------------------------------------------------------

void tryDHCP() {
  Serial.println("Trying DHCP...");
  if (Ethernet.begin(mac, 15000, 15000) == 0) {
    Serial.println("DHCP failed, using static IP");
    Ethernet.begin(mac, staticIP, staticDNS, staticGW, staticMask);
  } else {
    Serial.println("DHCP success");
    ethConnected = true;
  }
  printIP();
  startMDNS();
  startUDP();
  restartUDPTask();
}

void printIP() {
  Serial.print("IP Address: ");
  Serial.println(Ethernet.localIP());
  Serial.print("Subnet Mask: ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("Gateway: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("DNS: ");
  Serial.println(Ethernet.dnsServerIP());
}

// --------------------------------------------------------

void checkEthernetLink() {
  EthernetLinkStatus link = Ethernet.linkStatus();
  if (link == LinkON && !ethConnected) {
    Serial.println("Ethernet link UP — reinitializing DHCP");
    ethConnected = true;
    tryDHCP();

  } else if (link == LinkOFF && ethConnected) {
    Serial.println("Ethernet link DOWN");
    ethConnected = false;
    mdnsStarted = false;
    stopUDP();
    stopUDPTask();
  }
}

void startMDNS() {
  if (mdnsStarted) return;  // prevent re-init
  delay(200);               // give netif time to stabilise

  if (mdns.begin(Ethernet.localIP(), "Tally32-eth-1")) {
    Serial.println("mDNS responder started");
    mdns.addServiceRecord("TallyLight._http", 80, MDNSServiceTCP);
    mdnsStarted = true;
  } else {
    Serial.println("mDNS start failed — will retry later");
  }
}

// -------------------------
// UDP start/stop helpers
// -------------------------
void startUDP() {
  // Start UDP
  if (useMulticast) {
    Serial.println("Joining multicast group...");
    if (Udp.beginMulticast(multicastIP, localPort)) {
      Serial.printf("UDP multicast listening on port %d\n", localPort);
      udpRunning = true;
    } else {
      Serial.println("Failed to start multicast UDP!");
    }
  } else {
    if (Udp.begin(localPort)) {
      Serial.printf("UDP unicast listening on port %d\n", localPort);
      udpRunning = true;
    } else {
      Serial.println("Failed to start UDP listener!");
    }
  }
}

void stopUDP() {
  if (udpRunning) {
    Udp.stop();
    udpRunning = false;
    Serial.println("[UDP] Stopped");
  }
}



