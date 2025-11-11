#include "LED.h"

void led_blink(int times)
{
    for(int i = 0; i < times; i++)
    {
        digitalWrite(LED_PIN, HIGH);
        delay(500);
        digitalWrite(LED_PIN, LOW);
        delay(500);
    }
}