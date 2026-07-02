// 定义LED、蜂鸣器的类
#include <Arduino.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "sensor.h"
#include "pins.h"
#include "config.h"
#include "driver/rtc_io.h"

// 初始化LED引脚
void LEDControler::led_init()
{
    pinMode(LED_PIN, OUTPUT);
}

void LEDControler::led_on()
{
    digitalWrite(LED_PIN, LED_ACTIVE_LEVEL);
}
void LEDControler::led_off()
{
    digitalWrite(LED_PIN, LED_INACTIVE_LEVEL);
}


// period 传入延时时间，单位毫秒
void LEDControler::blink(LED_PERIOD period)
{
    // 解绑LEDC通道，重新设置为输出模式
    pinMode(LED_PIN, OUTPUT);

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
    ledcDetach(LED_PIN);
}


// 电池电量采样初始化
void PowerManager::power_init()
{
    // 配置电池电量采样引脚
    pinMode(BATTERY_PIN, INPUT);
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
    if(Vbattf > BATTERY_HIGH_THRESHOLD)
    {
        Vbattf = BATTERY_HIGH_THRESHOLD;
    }
    // 输出电压值
    ESP_LOGI(POWER_TAG, "battery voltage: %.3f", Vbattf);
    // 计算电池电量百分比
    uint8_t bat_perc = (Vbattf - BATTERY_LOW_THRESHOLD) * 100.0f / (BATTERY_HIGH_THRESHOLD - BATTERY_LOW_THRESHOLD) + 0.5f;
    ESP_LOGI(POWER_TAG, "battery percentage: %d%%", bat_perc);
    return bat_perc;
}

// 检测唤醒源：ESP-IDF API + 读按钮电平
void PowerManager::detectWakeupSource()
{
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

    // 等一小段时间让 GPIO 电平稳定后再读
    vTaskDelay(pdMS_TO_TICKS(20));
    bool user_pressed = (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED);
    bool dev_pressed  = (digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED);

    switch (reason)
    {


// #ifdef OUTSIDE
    // C6 的 EXT1 唤醒可以通过 bitmask 判断具体引脚
    case ESP_SLEEP_WAKEUP_EXT1:
    {
        uint64_t mask = esp_sleep_get_ext1_wakeup_status();
        bool user_ext = (mask & (1ULL << USER_BUTTON_PIN));
        bool dev_ext  = (mask & (1ULL << DEV_BUTTON_PIN));

        if (user_ext && dev_ext) {
            _wakeup_src = WAKEUP_BOTH_BUTTONS;
            ESP_LOGI(POWER_TAG, "Wakeup: BOTH buttons (EXT1)");
        } else if (user_ext) {
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: USER button (EXT1)");
        } else if (dev_ext) {
            _wakeup_src = WAKEUP_DEV_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: DEV button (EXT1)");
        } else {
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGW(POWER_TAG, "Wakeup: EXT1 but unknown pin, default USER");
        }
        break;
    }

    // C6 的 GPIO 唤醒 = Lora 唤醒
    case ESP_SLEEP_WAKEUP_GPIO:
        _wakeup_src = WAKEUP_LORA;
        ESP_LOGI(POWER_TAG, "Wakeup: Lora");
        break;
// #endif

    default:
        _wakeup_src = WAKEUP_POWER_ON;
        ESP_LOGI(POWER_TAG, "Wakeup: power on / reset (reason=%d)", reason);
        break;
    }
}

// 等待唤醒按键电平复位
void PowerManager::wait_wakeup_button_intend()
{

    while(gpio_get_level(USER_BUTTON_PIN) == BUTTON_PRESSED || gpio_get_level(DEV_BUTTON_PIN) == BUTTON_PRESSED)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

}

// 进入睡眠模式
void PowerManager::deep_sleep()
{
    // 等待唤醒引脚电平复位
    wait_wakeup_button_intend();
    // 配置按键唤醒引脚
// #ifdef OUTSIDE
    //  配置按键唤醒源
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKEUP_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    // 配置Lora唤醒源
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(LORA_WAKE_PIN),
        LORA_WAKE_ACTIVE ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
// #endif
    // 进入睡眠
    ESP_LOGI(POWER_TAG, "Going to sleep now");
    esp_deep_sleep_start();
    // 不会执行到这里
}

// 按键唤醒引脚初始化为输入模式
void PowerManager::wakeup_gpio_init()
{
// 配置按键唤醒引脚

// #ifdef OUTSIDE
    pinMode(LORA_WAKE_PIN, INPUT);
// #endif
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}







