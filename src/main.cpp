#include <Arduino.h>
#include <BfButton.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <millisDelay.h>
#include "nimbleTCodeUtils.h"

#define DEVICE_NAME "NimbleTCodeWifi"

WiFiManager wm;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#define FIRMWAREVERSION "NimbleStroker_TCode_Wifi_v0.1"

NimbleTCode nimble(FIRMWAREVERSION);

BfButton btn(BfButton::STANDALONE_DIGITAL, ENC_BUTT, true, LOW);

void pressHandler(BfButton *btn, BfButton::press_pattern_t pattern)
{
    switch (pattern)
    {
    case BfButton::SINGLE_PRESS:
        nimble.toggle();
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
millisDelay logDelay;

void logTimer()
{
    if (!logDelay.justFinished()) return;
    logDelay.repeat();

    nimble.printFrameState();
}

bool wifiLedState;

void updateLEDs()
{
    if (!ledDelay.justFinished()) return;
    ledDelay.repeat();

    if (wm.getConfigPortalActive()) {
        wifiLedState = (millis() % 500 > 250); // blink every 500ms
    } else {
        wifiLedState = true;
    }

    nimble.updateEncoderLEDs();
    nimble.updateHardwareLEDs();
    nimble.updateNetworkLEDs(
        (nimble.isRunning()) ? 50 : 0,
        (wifiLedState) ? 50 : 0
    );
}

size_t lastNumClients = 0;

void cleanupClients()
{
    if (!clientCleanupDelay.justFinished()) return;
    clientCleanupDelay.repeat();

    ws.cleanupClients();
    size_t tmpNumClients = ws.count();
    if (tmpNumClients == 0 && lastNumClients > 0) {
        nimble.stop();
    }
    lastNumClients = tmpNumClients;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        nimble.start();
#ifdef DEBUG
        Serial.printf("ws[%s][%u] Client connection\n", server->url(), client->id());
#endif
        break;

    case WS_EVT_DISCONNECT:
#ifdef DEBUG
        Serial.printf("ws[%s][%u] Client disconnect\n", server->url(), client->id());
#endif
        break;

    case WS_EVT_ERROR:
#ifdef DEBUG
        Serial.printf("ws[%s][%u] Error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
#endif
        break;

    case WS_EVT_PONG:
#ifdef DEBUG
        Serial.printf("ws[%s][%u] Pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
#endif
        break;

    case WS_EVT_DATA:
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
#ifdef DEBUG
        // Serial.printf("ws[%s][%u] %s data[%llu]\n", server->url(), client->id(), (info->opcode == WS_TEXT) ? "Text" : "Binary", info->len);
        // if (info->opcode == WS_TEXT) {
        //     data[len] = 0;
        //     Serial.printf("Message: %s\n", (char*)data);
        // }
#endif
        for (size_t i=0; i < info->len; i++) {
            nimble.inputByte(data[i]);
        }
        break;
    }
}

void setupNetwork()
{
    wm.setDarkMode(true);
    wm.setMinimumSignalQuality(10);
    wm.setConfigPortalBlocking(false);
    wm.setHostname(DEVICE_NAME);
    if (wm.autoConnect(DEVICE_NAME)) {
        ws.onEvent(onWsEvent);
        server.addHandler(&ws);
        server.begin();
    }
    clientCleanupDelay.start(1000);
}

void setup()
{
    // Misc init
    WiFi.mode(WIFI_STA);    // force initial wifi setting for ESP32
    nimble.init();          // initialize NimbleConModule devices
    nimble.stop();
    while (!Serial);

#ifdef DEBUG
    Serial.setDebugOutput(true);
    delay(200);
    Serial.println("\nStarting");
#else
    Serial.setDebugOutput(false);
#endif

    setupNetwork();

    // Encoder button press events
    btn.onPress(pressHandler)
        .onPressFor(pressHandler, 2000);

    ledDelay.start(30);
    logDelay.start(1000);
}

void loop()
{
    btn.read();
    wm.process();
    nimble.updateActuator();
    cleanupClients();
    updateLEDs();
#ifdef DEBUG
    //logTimer();
#endif
}