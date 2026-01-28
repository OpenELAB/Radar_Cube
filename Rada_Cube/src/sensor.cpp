// 定义LED、蜂鸣器的类
#include <Arduino.h>
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "sensor.h"
#include "pins.h"
#include "config.h"

// 初始化LED引脚
void LEDControler::init()
{
    pinMode(LED_PIN, OUTPUT);
}

// period 传入延时时间，单位毫秒
void LEDControler::blink(LED_PERIOD period)
{
    const uint32_t led_period = period / 2;
    digitalWrite(LED_PIN, LED_ACTIVE_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(led_period));
    digitalWrite(LED_PIN, LED_INACTIVE_LEVEL);
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
void BeeperControler::beep_stop()
{
    ledcWrite(BEEPER_PIN, 0);
}

// 电池电量采样初始化
void PowerManager::init()
{
    // 配置电池电量采样引脚
    pinMode(BATTERY_PIN, INPUT);
    // 配置按键唤醒引脚
    pinMode(WAKE_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}

// 读取电池电量值
uint8_t PowerManager::get_battery_value()
{
    uint32_t Vbatt = 0;

    // 采样32次，取平均值
    for(int i = 0; i < 32; i++)
    {
        Vbatt += analogReadMilliVolts(BATTERY_PIN);
    }
    // 1：2 分压，计算实际电压值
    float Vbattf = (Vbatt * 2) / 32.0 / 1000.0;
    if (Vbattf < BATTERY_LOW_THRESHOLD)
    {
        Vbattf = BATTERY_LOW_THRESHOLD;
    }
    if(Vbatt > BATTERY_HIGH_THRESHOLD)
    {
        Vbattf = BATTERY_HIGH_THRESHOLD;
    }
    // 输出电压值
    printf("battery voltage: %.3f\n", Vbattf);
    // 计算电池电量百分比
    uint8_t bat_perc = (Vbattf - BATTERY_LOW_THRESHOLD) * 100.0f / (BATTERY_HIGH_THRESHOLD - BATTERY_LOW_THRESHOLD) + 0.5f;
    printf("battery percentage: %d%%\n", bat_perc);
    return bat_perc;
}

// 获取醒来原因
void PowerManager::get_wakeup_reason()
{
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_GPIO:
        {
            printf("[Info] Wakeup cause by RTC GPIO\r\n");
            break;
        }
        default:
            printf("[Info] Wakeup cause by other: %d\r\n", wakeup_reason);
            break;
    }
}

// 等待唤醒按键电平复位
void PowerManager::wait_wakeup_button_intend()
{
   while(gpio_get_level(WAKE_BUTTON_PIN) == GPIO_INACTIVE_LEVEL  || gpio_get_level(DEV_BUTTON_PIN) == GPIO_INACTIVE_LEVEL)
   {
        vTaskDelay(pdMS_TO_TICKS(10));
   }
}

// 进入睡眠模式
esp_err_t PowerManager::deep_sleep()
{
    // 等待唤醒引脚电平复位
    wait_wakeup_button_intend();
    // 配置按键唤醒引脚
    esp_deep_sleep_enable_gpio_wakeup(BIT(WAKE_BUTTON_PIN) | BIT(DEV_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
    // 进入睡眠
    printf("[Info] Going to sleep\r\n");
    esp_deep_sleep_start();
    // 不会执行到这里
    return ESP_OK;
}

// 按键唤醒引脚初始化为输入模式
void PowerManager::wakeup_gpio_init()
{
    pinMode(WAKE_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}


// 按键扫描
void PowerManager::wake_button_detection()
{
    vTaskDelay(pdMS_TO_TICKS(20));
    bool wake_button_level = digitalRead(WAKE_BUTTON_PIN);
    bool dev_button_level = digitalRead(DEV_BUTTON_PIN);
    printf("[Info] wake_button_level: %d, dev_button_level: %d\n", wake_button_level, dev_button_level);
}



