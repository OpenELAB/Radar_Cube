#ifndef __SR04_H__
#define __SR04_H__

#include <Arduino.h>

#define ECHO_PIN  3   // 输入回声信号引脚
#define TRIG_PIN  4   // 输出触发信号引脚


class SR04
{
public:
    void sr04_init();
    uint16_t get_distance();
private:
};



#endif