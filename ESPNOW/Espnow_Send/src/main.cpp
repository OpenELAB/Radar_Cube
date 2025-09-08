#include <Arduino.h>
#include <espnow.h>
#include <sr04.h>

ESPNOW_Rada rada_broad;
SR04 rada_sensor;

void setup()
{
    Serial.begin(115200);
    rada_broad.Rada_rece_init();
    rada_broad.unicast_mode();
    rada_sensor.sr04_init();


}

void loop()
{
    uint16_t d = rada_sensor.get_distance();
    if(d> 20 && d < 600) 
    {
        Serial.println(d);
        rada_broad.send_rada_data((uint8_t*)&d, sizeof(d));
    }

    delay(20);

}