#include <Arduino.h>
#include "nimbleConModule.h"

#define MAX_POSITION_DELTA 50

int16_t framePosition = 0; // next position to send to actuator (-1000 to 1000)
int16_t targetPos = 0; // position controlled via TCode
int16_t lastFramePos = 0;  // positon that was sent to the actuator last loop frame
int16_t frameForce = IDLE_FORCE; // next force to send to the actuator (0 to 1023)

#define VIBRATION_MAX_AMP 25
#define VIBRATION_MAX_SPEED 20.0 // hz

float vibrationSpeed = VIBRATION_MAX_SPEED; // hz
uint16_t vibrationAmplitude = 0; // in position units (0 to 25)
int16_t vibrationPos = 0;

#define RUN_MODE_OFF 0
#define RUN_MODE_ON 1

bool runMode = RUN_MODE_ON;

int16_t clampPositionDelta() {
    int16_t delta = framePosition - lastFramePos;
    if (delta >= 0) {
        return (delta > MAX_POSITION_DELTA) ? lastFramePos + MAX_POSITION_DELTA : framePosition;
    } else {
        return (delta < -MAX_POSITION_DELTA) ? lastFramePos - MAX_POSITION_DELTA : framePosition;
    }
}

void printNimbleState()
{
    Serial.printf("------------------\n");
    Serial.printf("   VibAmp: %5d\n", vibrationAmplitude);
    Serial.printf(" VibSpeed: %0.2f (hz)\n", vibrationSpeed);
    Serial.printf("   TarPos: %5d\n", targetPos);
    Serial.printf("      Pos: %5d\n", lastFramePos);
    Serial.printf("    Force: %5d\n", frameForce);
    Serial.printf("    AirIn: %5d\n", actuator.airIn);
    Serial.printf("   AirOut: %5d\n", actuator.airOut);
    Serial.printf("TempLimit: %s\n",  actuator.tempLimiting ? "true" : "false");
}

void ledPulse(short int pos, short int vibPos, bool isOn = true)
{
    byte ledScale = map(abs(pos), 0, ACTUATOR_MAX_POS, 1, LED_MAX_DUTY);
    byte ledState1 = 0;
    byte ledState2 = 0;
    byte vibScale = map(abs(vibPos), 0, VIBRATION_MAX_AMP, 1, LED_MAX_DUTY);
    byte vibState1 = 0;
    byte vibState2 = 0;

    if (isOn)
    {
        if (pos < 0) {
            ledState1 = ledScale;
        } else if (pos > 0) {
            ledState2 = ledScale;
        }

        if (vibPos < 0) {
            vibState1 = vibScale;
        } else if (vibPos > 0) {
            vibState2 = vibScale;
        }
    }

    ledcWrite(ENC_LED_N,  ledState1);
    ledcWrite(ENC_LED_SE, ledState1);
    ledcWrite(ENC_LED_SW, ledState1);

    ledcWrite(ENC_LED_NE, ledState2);
    ledcWrite(ENC_LED_NW, ledState2);
    ledcWrite(ENC_LED_S,  ledState2);

    ledcWrite(ENC_LED_W, vibState1);
    ledcWrite(ENC_LED_E, vibState2);
}