#include "sr04.h"

void  SR04::sr04_init()
{
    pinMode(ECHO_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
}

uint16_t SR04::get_distance()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    uint32_t duration = pulseIn(ECHO_PIN, HIGH, 38000);
    return duration ? duration / 58 : 0;
}




