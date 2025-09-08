#ifndef __BUZZER_H__
#define __BUZZER_H__

#include <Arduino.h>
#include <esp32-hal-ledc.h>

#define BUZZER_PIN 10
#define BUZZER_FREQ 2500
#define BUZZER_VOLUME 0

class Buzzer
{
private:
    uint8_t _pin;
    uint32_t _freq;

public:
    explicit Buzzer(uint8_t pin, uint32_t freq);
    ~Buzzer();
    void begin();
    void play(uint32_t volume = 0);
    void stop();
};




#endif
