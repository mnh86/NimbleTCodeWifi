#include <Arduino.h>
#include <BfButton.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <millisDelay.h>
#include "nimbleConModule.h"

#define DEVICE_NAME "NimbleTCodeWifi"

WiFiManager wm;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

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

millisDelay clientCleanupDelay;
millisDelay ledDelay;
bool wifiLedState;

void updateLEDs()
{
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

void cleanupClients()
{
    if (!clientCleanupDelay.justFinished()) return;
    clientCleanupDelay.repeat();

    ws.cleanupClients();
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("ws[%s][%u] Client connection\n", server->url(), client->id());
        break;

    case WS_EVT_DISCONNECT:
        Serial.printf("ws[%s][%u] Client disconnect\n", server->url(), client->id());
        break;

    case WS_EVT_ERROR:
        Serial.printf("ws[%s][%u] Error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
        break;

    case WS_EVT_PONG:
        Serial.printf("ws[%s][%u] Pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
        break;

    case WS_EVT_DATA:
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        Serial.printf("ws[%s][%u] Data packet[%u][%u][%u]\n", server->url(), client->id(), info->final, info->index, info->len);
        break;
    }
}

void setup()
{
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
    if (wm.autoConnect(DEVICE_NAME)) {
        ws.onEvent(onWsEvent);
        server.addHandler(&ws);
        server.begin();
    }

    // Encoder button press events
    btn.onDoublePress(pressHandler)
        .onPressFor(pressHandler, 2000);

    ledDelay.start(500);
    clientCleanupDelay.start(1000);
}

void loop()
{
    btn.read();
    wm.process();
    updateLEDs();
    cleanupClients();
}