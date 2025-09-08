#include "buzzer.h"



Buzzer::Buzzer(uint8_t pin, uint32_t freq)
: _pin(pin), _freq(freq)
{
}

Buzzer::~Buzzer()
{
    ledcWrite(_pin, 0);
}

void Buzzer::begin()
{
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
    ledcAttach(_pin, _freq, 8);
}

void Buzzer::play(uint32_t volume)
{
    ledcWrite(_pin, volume);
}

void Buzzer::stop()
{
    ledcWrite(_pin, 0);
}



