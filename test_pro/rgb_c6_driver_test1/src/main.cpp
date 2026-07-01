#include <Arduino.h>
#include "rgb_led_controller.h"

static RgbLedController RgbLed;

constexpr uint32_t EFFECT_SWITCH_MS = 5000;
uint32_t last_switch_ms = 0;
uint8_t effect_index = 0;

void setup()
{
    RgbLed.begin();
    RgbLed.setBrightness(RgbBrightnessLevel::Low);
    RgbLed.solid(RGB_COLOR_GREEN);
    last_switch_ms = millis();
}

void loop()
{
    if (millis() - last_switch_ms < EFFECT_SWITCH_MS) {
        delay(20);
        return;
    }

    last_switch_ms = millis();
    effect_index = (effect_index + 1) % 4;

    switch (effect_index) {
    case 0:
        RgbLed.solid(RGB_COLOR_ORANGE);
        break;
    case 1:
        RgbLed.breathe(RGB_COLOR_BLUE, 1800);
        break;
    case 2:
        RgbLed.blink(RGB_COLOR_YELLOW, 600);
        break;
    case 3:
    default:
        RgbLed.blink(RGB_COLOR_RED, 250);
        break;
    }
}
