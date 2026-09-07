#include "arbiter.h"
#include <WiFi.h>
#include <SocketIOclient.h>
#include <ArduinoJson.h>

// Protocol (Tally Arbiter 3.x, socket.io v4 with allowEIO3, port 4455):
//   connect  -> emit listenerclient_connect {deviceId, listenerType, canBeReassigned, canBeFlashed, supportsChat}
//   server   -> bus_options [{id,label,type,color,priority}], devices [{id,name}],
//               device_states [{deviceId,busId,sources[]}] (filtered to our device),
//               deviceId "<id>", reassign "<old>","<new>", flash
// Tally: for every device_states entry with sources, the bus type sets a bit:
// program = 2, preview = 1. Same 0-3 value as the TSL control byte.

#define TA_MAX_BUSES   16
#define TA_MAX_DEVICES 64
#define TA_STACK       8192

static SocketIOclient socket;
static TaskHandle_t taTask = NULL;
static SemaphoreHandle_t taMutex = NULL;  // guards the Strings the web server reads
static volatile bool connected = false;
static volatile bool wantReconnect = false;
static String devicesJson = "[]";
static String deviceName = "";
static uint32_t serverIp = 0;
static volatile uint32_t nStates = 0, nFlashes = 0, nReassigns = 0;  // for the page

struct Bus { String id; uint8_t type; };  // type: 1 preview, 2 program, 0 other
static Bus buses[TA_MAX_BUSES];
static int busCount = 0;

// The web server reads these before the task exists, so the mutex is made on first use
// (always from core 1: setup() and the web server both run there)
static void ensureMutex() {
  if (!taMutex) taMutex = xSemaphoreCreateMutex();
}

static String lockedCopy(const String& s) {
  ensureMutex();
  xSemaphoreTake(taMutex, portMAX_DELAY);
  String c = s;
  xSemaphoreGive(taMutex);
  return c;
}

static void lockedSet(String& dst, const String& v) {
  ensureMutex();
  xSemaphoreTake(taMutex, portMAX_DELAY);
  dst = v;
  xSemaphoreGive(taMutex);
}

bool   arbiterConnected() { return taTask != NULL && connected; }
String arbiterDeviceName() { return lockedCopy(deviceName); }
String arbiterDevicesJson() { return lockedCopy(devicesJson); }
String arbiterStatsJson() {
  return "{\"states\":" + String((unsigned long)nStates) + ",\"flashes\":" + String((unsigned long)nFlashes) + ",\"reassigns\":" + String((unsigned long)nReassigns) + "}";
}

// socket.io event frame: ["name", arg...]
static void emitJson(const char* event, const String& json) {
  String p = String("[\"") + event + "\"";
  if (json.length()) p += "," + json;
  p += "]";
  socket.sendEVENT(p);
}

static void sendHello() {
  JsonDocument d;
  d["deviceId"] = taDeviceId;
  d["listenerType"] = "Video Walrus " + deviceHostname;  // shown in Tally Arbiter's listener list
  d["canBeReassigned"] = true;
  d["canBeFlashed"] = true;
  d["supportsChat"] = false;
  String j;
  serializeJson(d, j);
  emitJson("listenerclient_connect", j);
  Serial.printf("[TA] Registered as device %s\n", taDeviceId.c_str());
}

// Name of our device from the devices list we hold
static void updateDeviceName() {
  String name;
  JsonDocument doc;
  if (!deserializeJson(doc, lockedCopy(devicesJson))) {
    for (JsonObject d : doc.as<JsonArray>()) {
      if (taDeviceId == (const char*)(d["id"] | "")) { name = (const char*)(d["name"] | ""); break; }
    }
  }
  lockedSet(deviceName, name);
}

static void flashWhite(int times) {
  ledOverride = true;  // hold off setTallyState() and the spin animation
  for (int i = 0; i < times; i++) {
    ledShowSolid(CRGB::White);
    delay(200);
    ledShowSolid(CRGB::Black);
    delay(200);
  }
  ledOverride = false;
  setTallyState(tslState, tslBrightness);
}

static void onBusOptions(JsonArray arr) {
  busCount = 0;
  for (JsonObject b : arr) {
    if (busCount >= TA_MAX_BUSES) break;
    const char* type = b["type"] | "";
    buses[busCount].id = (const char*)(b["id"] | "");
    buses[busCount].type = !strcmp(type, "program") ? 2 : !strcmp(type, "preview") ? 1 : 0;
    busCount++;
  }
  Serial.printf("[TA] %d buses\n", busCount);
}

static void onDevices(JsonArray arr) {
  JsonDocument out;
  JsonArray o = out.to<JsonArray>();
  int n = 0;
  for (JsonObject d : arr) {
    if (n++ >= TA_MAX_DEVICES) break;
    JsonObject e = o.add<JsonObject>();
    e["id"] = d["id"];
    e["name"] = d["name"];
  }
  String j;
  serializeJson(out, j);
  lockedSet(devicesJson, j);
  updateDeviceName();
  Serial.printf("[TA] %d devices, ours: %s\n", n, arbiterDeviceName().c_str());
}

static void onDeviceStates(JsonArray arr) {
  int T = 0;
  for (JsonObject st : arr) {
    const char* dev = st["deviceId"] | "";
    if (dev[0] && taDeviceId != "unassigned" && taDeviceId != dev) continue;  // not ours
    JsonArray sources = st["sources"];
    if (sources.isNull() || sources.size() == 0) continue;
    const char* busId = st["busId"] | "";
    for (int i = 0; i < busCount; i++) {
      if (buses[i].id == busId) T |= buses[i].type;
    }
  }
  tslState = T;
  tslBrightRaw = -1;
  tslBrightness = -1;  // Tally Arbiter has no brightness, use the configured maximum
  setTallyState(T, -1);
  nStates++;
  tslPackets++;
  tslLastMs = millis();
  tslLastFrom = serverIp;
}

static void setDevice(const String& id) {
  if (id.length() == 0 || id == taDeviceId) return;
  taDeviceId = id;
  saveSettings();
  updateDeviceName();
}

static void onSocketEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case sIOtype_CONNECT:
      connected = true;
      Serial.println("[TA] Connected");
      sendHello();
      break;
    case sIOtype_DISCONNECT:
      if (connected) Serial.println("[TA] Disconnected");
      connected = false;
      break;
    case sIOtype_EVENT: {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) { Serial.printf("[TA] Bad event: %s\n", err.c_str()); break; }
      const char* ev = doc[0] | "";
      if (!strcmp(ev, "bus_options")) onBusOptions(doc[1].as<JsonArray>());
      else if (!strcmp(ev, "devices")) onDevices(doc[1].as<JsonArray>());
      else if (!strcmp(ev, "device_states")) onDeviceStates(doc[1].as<JsonArray>());
      else if (!strcmp(ev, "deviceId")) {
        // Server picked a device for us (we connected as "unassigned" or with a stale id)
        setDevice((const char*)(doc[1] | ""));
        Serial.printf("[TA] Assigned device %s\n", taDeviceId.c_str());
      } else if (!strcmp(ev, "reassign")) {
        String oldId = (const char*)(doc[1] | ""), newId = (const char*)(doc[2] | "");
        Serial.printf("[TA] Reassigned %s -> %s\n", oldId.c_str(), newId.c_str());
        setDevice(newId);
        JsonDocument r;
        r["oldDeviceId"] = oldId;
        r["newDeviceId"] = newId;
        String j;
        serializeJson(r, j);
        emitJson("listener_reassign_object", j);
        nReassigns++;
        flashWhite(2);
        // The server does not reliably move this socket to the new device's room after
        // the ack above, so register again from scratch with the new id.
        wantReconnect = true;
      } else if (!strcmp(ev, "flash")) {
        Serial.println("[TA] Flash");
        nFlashes++;
        flashWhite(3);
      }
      break;
    }
    default: break;
  }
}

static void resolveServer() {
  IPAddress ip;
  if (ip.fromString(taHost) || WiFi.hostByName(taHost.c_str(), ip)) serverIp = (uint32_t)ip;
}

static void taTaskFn(void*) {
  Serial.printf("[TA] Task on core %d, server %s:%d\n", xPortGetCoreID(), taHost.c_str(), taPort);
  resolveServer();
  socket.onEvent(onSocketEvent);
  socket.setReconnectInterval(5000);
  socket.begin(taHost, (uint16_t)taPort);
  for (;;) {
    if (wantReconnect) {
      wantReconnect = false;
      connected = false;
      socket.disconnect();
      resolveServer();
      socket.begin(taHost, (uint16_t)taPort);
    }
    socket.loop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void arbiterStart() {
  if (taTask != NULL) return;
  if (taHost.length() == 0) {
    Serial.println("[TA] No server host configured");
    return;
  }
  ensureMutex();
  xTaskCreatePinnedToCore(taTaskFn, "TA Task", TA_STACK, NULL, 1, &taTask, 0);  // core 0, like the UDP task
}

void arbiterReconnect() {
  if (taTask != NULL) wantReconnect = true;
}
