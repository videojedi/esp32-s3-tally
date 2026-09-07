/*
    Tally Arbiter listener client. Connects to a Tally Arbiter server over socket.io,
    registers as a listener for one of its devices and drives the tally from the
    device_states it sends. Alternative to the TSL 3.1 multicast listener.
    Video Walrus 2026
*/
#pragma once
#include <Arduino.h>
#include <FastLED.h>

// Provided by main.cpp
void setTallyState(int state, int brightness);
void saveSettings();
void ledShowSolid(CRGB colour);
extern volatile bool ledOverride;
extern volatile int tslState, tslBrightness, tslBrightRaw;
extern volatile uint32_t tslPackets;
extern volatile unsigned long tslLastMs;
extern volatile uint32_t tslLastFrom;
extern String taHost, taDeviceId, deviceHostname;
extern int taPort;

void   arbiterStart();          // start the client task; needs taHost set and the network up
void   arbiterReconnect();      // drop and re-open the socket, e.g. after the device changed
bool   arbiterConnected();
String arbiterDeviceName();     // name of the assigned device, "" until the server has told us
String arbiterDevicesJson();    // [{"id":"..","name":".."},...] as last sent by the server
String arbiterStatsJson();      // {"states":n,"flashes":n,"reassigns":n} received from the server
