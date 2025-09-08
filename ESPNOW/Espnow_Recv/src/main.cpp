#include <Arduino.h>
#include "espnow.h"
#include "buzzer.h"


ESPNOW_Buzzer broadcaster;
Buzzer buzzer(10, 2500);

void setup()
{
    Serial.begin(115200);
    broadcaster.broadcast_init();
    broadcaster.broadcast_loop();
    broadcaster.unicast_mode();
    buzzer.begin();
    // buzzer.play(128);
    // delay(10000);

}

void loop()
{
    static uint32_t nextToggle = 0;
    static bool buzzer_on = false;
    static bool long_beep = false;
    uint16_t distant = broadcaster.getMedianDistance();
    if (distant < 20) 
    {
        return;
    }
    Serial.println(distant);

    if(distant < 25)
    {
        if(!long_beep)
        {
            buzzer.play(1);
            long_beep = true;
        }
        return;
    }
    else
    {
        if(long_beep)
        {
            buzzer.stop();
            long_beep = false;
        }
    }

    if(distant > 150)
    {
        buzzer.stop();
        return;
    }


    uint16_t period = map(distant, 25, 150, 50, 1000);
    uint8_t  duty = 1;
    // duty = map(distant, 25, 150, 128, 0);

    uint32_t now = millis();
    if(period == 0)
    {
        buzzer.stop();
        return;
    }
    if(now >= nextToggle)
    {
        buzzer_on = !buzzer_on;

        buzzer.play(buzzer_on ? duty : 0);

        nextToggle = now + (period / 2);
    }

}