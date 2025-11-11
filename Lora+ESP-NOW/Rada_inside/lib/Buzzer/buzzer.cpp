#include "buzzer.h"


void buzzer_init()
{
    ledcAttach(BUZZER_PIN, BUZZER_FREQ, BUZZER_RESOLUTION);
}

void buzzer_on(int duty, int times)
{
    ledcWrite(BUZZER_PIN, duty);
    for(int i = 0; i < times; i++)
    {
        delay(1000);
    }
    ledcWrite(BUZZER_PIN, 0);
}