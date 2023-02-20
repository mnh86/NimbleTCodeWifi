#include <Arduino.h>
#include <BfButton.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <TCode.h>
#include <millisDelay.h>
#include "nimbleUtils.h"

#define DEVICE_NAME "NimbleTCodeWifi"

WiFiManager wm;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#define FIRMWAREVERSION "NimbleStroker_TCode_Wifi_v0.1"

TCode<3> tcode(FIRMWAREVERSION);

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
millisDelay logDelay;
bool wifiLedState;

void logTimer()
{
    if (!logDelay.justFinished()) return;
    logDelay.repeat();

    printNimbleState();
}

void updateLEDs()
{
    if (!ledDelay.justFinished()) return;
    ledDelay.repeat();

    if (wm.getConfigPortalActive()) {
        wifiLedState = (millis() % 500 > 250); // blink every 500ms
    } else {
        wifiLedState = true;
    }

    ledPulse(lastFramePos, vibrationPos);
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
    if (ws.count() == 0) {
        tcode.stop(); // TODO: only stop if tcode buffer has size
    }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
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
            tcode.inputByte(data[i]);
        }
        break;
    }
}

void initTCodeAxes()
{
    tcode.init();
    tcode.axisRegister("L0", F("Up")); // Up stroke position
    tcode.axisWrite("L0", 5000, ' ', 0); // 5000: midpoint
    tcode.axisEasingType("L0", EasingType::EASEINOUT);

    tcode.axisRegister("V0", F("Vibe")); // Vibration Amplitude
    tcode.axisWrite("V0", 0, ' ', 0);    // 0: vibration off
    tcode.axisEasingType("V0", EasingType::EASEINOUT);

    tcode.axisRegister("A0", F("Air")); // Air in/out valve
    tcode.axisWrite("A0", 5000, ' ', 0); // 0: air out, 5000: stop, 9999: air in

    tcode.axisRegister("A1", F("Force"));
    tcode.axisWrite("A1", 9999, ' ', 0); // 9999: max force

    tcode.axisRegister("A2", F("VibeSpeed"));
    tcode.axisWrite("A2", 9999, ' ', 0); // 9999: max vibration speed
}

/**
 * L0 Up position is mixed with a V0/A2 vibration oscillator
 */
void positionHandler()
{
    if (!tcode.axisChanged("L0")) {
        int val = tcode.axisRead("L0");
        targetPos = map(val, 0, 9999, -ACTUATOR_MAX_POS, ACTUATOR_MAX_POS);
    }

    if (tcode.axisChanged("V0")) {
        int val = tcode.axisRead("V0");
        vibrationAmplitude = map(val, 0, 9999, 0, VIBRATION_MAX_AMP);
    }

    if (vibrationAmplitude > 0 && vibrationSpeed > 0) {
        int vibSpeedMillis = 1000 / vibrationSpeed;
        int vibModMillis = millis() % vibSpeedMillis;
        float tempPos = float(vibModMillis) / vibSpeedMillis;
        int vibWaveDeg = tempPos * 360;
        vibrationPos = round(sin(radians(vibWaveDeg)) * vibrationAmplitude);
    } else {
        vibrationPos = 0;
    }
    // Serial.printf("A:%5d S:%0.2f P:%5d\n",
    //     vibrationAmplitude,
    //     vibrationSpeed,
    //     vibrationPos
    // );

    int targetPosTmp = targetPos;
    if (targetPos - vibrationAmplitude < -ACTUATOR_MAX_POS) {
        targetPosTmp = targetPos + vibrationAmplitude;
    } else if (targetPos + vibrationAmplitude > ACTUATOR_MAX_POS) {
        targetPosTmp = targetPos - vibrationAmplitude;
    }
    framePosition = targetPosTmp + vibrationPos;
}

void airHandler()
{
    if (!tcode.axisChanged("A0")) return;
    int val = tcode.axisRead("A0");
    int airState = map(val, 0, 9999, -1, 1);
    actuator.airIn = (airState > 0);
    actuator.airOut = (airState < 0);
}

void forceHandler()
{
    if (!tcode.axisChanged("A1")) return;
    int val = tcode.axisRead("A1");
    frameForce = map(val, 0, 9999, 0, MAX_FORCE);
}

void vibrationSpeedHandler()
{
    if (!tcode.axisChanged("A2")) return;
    int val = tcode.axisRead("A2");
    int maxSpeed = VIBRATION_MAX_SPEED * 100;
    int newSpeed = map(val, 0, 9999, 0, maxSpeed);
    vibrationSpeed = float(newSpeed) / 100;
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
        initTCodeAxes();
    }

    // Encoder button press events
    btn.onDoublePress(pressHandler)
        .onPressFor(pressHandler, 2000);

    ledDelay.start(30);
    clientCleanupDelay.start(1000);
    logDelay.start(1000);
}

void loop()
{
    btn.read();
    wm.process();

    vibrationSpeedHandler();
    positionHandler();
    airHandler();
    forceHandler();

    // Send packet of values to the actuator when time is ready
    if (checkTimer())
    {
        if (runMode == RUN_MODE_ON) {
            lastFramePos = clampPositionDelta();
            actuator.positionCommand = lastFramePos;
            actuator.forceCommand = frameForce;
        }
        sendToAct();
    }

    if (readFromAct()) // Read current state from actuator.
    { // If the function returns true, the values were updated.

        // Unclear yet if any action is required when tempLimiting is occurring.
        // A comparison is needed with the Pendant behavior.
        // if (actuator.tempLimiting) {
        //     setRunMode(RUN_MODE_OFF);
        // }

        // Serial.printf("A P:%4d F:%4d T:%s\n",
        //     actuator.positionFeedback,
        //     actuator.forceFeedback,
        //     actuator.tempLimiting ? "true" : "false"
        // );
    }

    updateLEDs();
    cleanupClients();
#ifdef DEBUG
    //logTimer();
#endif
}