#include <Arduino.h>
#include <BfButton.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <millisDelay.h>
#include "nimbleConModule.h"

#define DEVICE_NAME "NimbleTCodeWifi"

WiFiManager wm;

BfButton btn(BfButton::STANDALONE_DIGITAL, ENC_BUTT, true, LOW);

void pressHandler(BfButton *btn, BfButton::press_pattern_t pattern)
{
    switch (pattern)
    {
    case BfButton::DOUBLE_PRESS:
        break;

    case BfButton::LONG_PRESS:
        // Reset Wifi settings and credentials
        if (!wm.getConfigPortalActive()) {
            wm.resetSettings();
            delay(1000);
            wm.reboot();
        }
        break;
    }
}

millisDelay ledDelay;
bool wifiLedState;

void updateLEDs() {
    if (!ledDelay.justFinished()) return;
    ledDelay.repeat();

    if (wm.getConfigPortalActive()) {
        wifiLedState = !wifiLedState; // blink
    } else {
        wifiLedState = true;
    }

    ledcWrite(BT_LED, 0);
    ledcWrite(WIFI_LED, (wifiLedState) ? 50 : 0);
    ledcWrite(PEND_LED, (pendant.present) ? 50 : 0);
    ledcWrite(ACT_LED, (actuator.present) ? 50 : 0);
}

void setup() {
    // Misc init
    WiFi.mode(WIFI_STA);    // force initial wifi setting for ESP32
    initNimbleConModule();  // initialize NimbleConModule devices
    while (!Serial);
#ifdef DEBUG
    Serial.setDebugOutput(true);
    delay(200);
    Serial.println("\nStarting");
#endif

    // Wifi manager settings
    wm.setDarkMode(true);
    wm.setMinimumSignalQuality(10);
    wm.setConfigPortalBlocking(false);
    wm.setHostname(DEVICE_NAME);
    wm.autoConnect(DEVICE_NAME);

    // Encoder button press events
    btn.onDoublePress(pressHandler)
        .onPressFor(pressHandler, 2000);

    ledDelay.start(500);
}

void loop() {
    btn.read();
    wm.process();
    updateLEDs();
}