#ifndef __SR04_H__
#define __SR04_H__

#include <Arduino.h>

#define ECHO_PIN  3   // Input echo signal pin
#define TRIG_PIN  4   // Output trigger signal pin


class SR04
{
public:
    void sr04_init();
    uint16_t get_distance();
private:
};



#endif