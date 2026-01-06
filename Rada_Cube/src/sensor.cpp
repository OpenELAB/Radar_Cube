// 定义LED、蜂鸣器的类
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "sensor.h"
#include "pins.h"

// 初始化LED引脚
void LEDControler::init()
{
    pinMode(LED_PIN, OUTPUT);
}

// period 传入延时时间，单位毫秒
void LEDControler::blink(LED_PERIOD period)
{
    const uint32_t led_period = period / 2;
    digitalWrite(LED_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(led_period));
    digitalWrite(LED_PIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(led_period));
}

// 传入延时速度，单位ms, 数值越小速度越快
void LEDControler::breath(LED_SPEED speed)
{
    ledcAttach(LED_PIN, LED_FREQ, LEDC_TIMER_8_BIT);
    for (int duty = 0; duty < 255; duty += 2)
    {
        ledcWrite(LED_PIN, duty);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
    for (int duty = 255; duty >= 0; duty -= 2)
    {
        ledcWrite(LED_PIN, duty);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
}

// 初始化蜂鸣器引脚
void BeeperControler::init()
{
    pinMode(BEEPER_PIN, OUTPUT);
    ledcAttach(BEEPER_PIN, BEEPER_FREQ, LEDC_TIMER_8_BIT);
}

// 蜂鸣器控制
void BeeperControler::beep(BEEP period)
{
    if(period == BEEPER_PERIOD_LONG)
    {
        ledcWrite(BEEPER_PIN, BEEPER_DUTY);
        return;
    }
    const uint32_t beep_period = period / 2;
    ledcWrite(BEEPER_PIN, BEEPER_DUTY);
    vTaskDelay(pdMS_TO_TICKS(beep_period));
    ledcWrite(BEEPER_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(beep_period));
}