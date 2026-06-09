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

#ifdef INSIDE
// 初始化蜂鸣器周期
const uint16_t BeeperControler::PERIODS[] = {
    0,           // 静音
    1,           // 长鸣
    250,         // 蜂鸣周期250ms
    500,         // 蜂鸣周期500ms
    1000         // 蜂鸣周期1000ms
};

// 查蜂鸣器周期表
uint16_t BeeperControler::get_period(uint8_t mode)
{
    if(mode > sizeof(PERIODS)/sizeof(PERIODS[0]))
    {
        return 0; // 默认静音
    }
    return PERIODS[mode];
}

// 初始化蜂鸣器引脚
void BeeperControler::beeper_init()
{
    pinMode(BEEPER_PIN, OUTPUT);
    // ESP32 Arduino v3.0+ 推荐使用 ledcAttach 来初始化固定频率，但是它的参数并不是第三个作为占空比位宽
    // 如果用 ledcWriteTone，它底层会自动配置通道
    ledcAttach(BEEPER_PIN, BEEPER_FREQ, 8); // 设置为 8-bit 分辨率
}

// 蜂鸣器控制
void BeeperControler::beep()
{
    // 这里如果用原来的 ledcWrite(BEEPER_PIN, BEEPER_DUTY);
    // 可能会因为经历了 ledcWriteTone 后硬件占空比计算方式改变导致声音极大变小。
    // 为了最大化响度 (50% 占空比)，8-bit 分辨率下 50% = 127。
    // 如果想要更响的声音，我们应该给 127。原代码中 BEEPER_DUTY = 4，这是一个极小的占空比，会导致声音很轻。
    
    // 直接使用更稳定的 ledcWriteTone 来产生恒定频率，最大化响度

    // 重构蜂鸣器响应,改成非阻塞式的，周期性鸣叫需要外部定时

    ledcWriteTone(BEEPER_PIN, BEEPER_FREQ);
}
void BeeperControler::beep_stop()
{
    ledcWriteTone(BEEPER_PIN, 0);
}

// 播放上升调的成功提示音
void BeeperControler::play_success_tone()
{
    uint32_t tones[] = {1046, 1318, 1568};
    for(int i = 0; i < 3; i++) {
        ledcWriteTone(BEEPER_PIN, tones[i]);
        vTaskDelay(pdMS_TO_TICKS(100));
        ledcWriteTone(BEEPER_PIN, 0); // 停音
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// 播放下降调的失败提示音
void BeeperControler::play_fail_tone()
{
    uint32_t tones[] = {1568, 1318, 1046, 784};
    for(int i = 0; i < 4; i++) {
        ledcWriteTone(BEEPER_PIN, tones[i]);
        vTaskDelay(pdMS_TO_TICKS(150)); 
    }
    
    ledcWriteTone(BEEPER_PIN, 0); 
}
#endif

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
#ifdef INSIDE
    // C3 的 GPIO 唤醒无法区分哪个引脚，靠读电平判断
    case ESP_SLEEP_WAKEUP_GPIO:
        if (user_pressed && dev_pressed) {
            _wakeup_src = WAKEUP_BOTH_BUTTONS;
            ESP_LOGI(POWER_TAG, "Wakeup: BOTH buttons pressed");
        } else if (user_pressed) {
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: USER button");
        } else if (dev_pressed) {
            _wakeup_src = WAKEUP_DEV_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: DEV button");
        } else {
            // 按键已松开，无法确定是哪个，默认 USER
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGW(POWER_TAG, "Wakeup: GPIO but no button held, default USER");
        }
        break;
#endif

#ifdef OUTSIDE
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
#endif

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
// 车内模块的唤醒引脚配置
#ifdef INSIDE
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(USER_BUTTON_PIN) | BIT(DEV_BUTTON_PIN),
        BUTTON_PRESSED ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif

#ifdef OUTSIDE
    //  配置按键唤醒源
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKEUP_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    // 配置Lora唤醒源
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(LORA_WAKE_PIN),
        LORA_WAKE_ACTIVE ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
    // 进入睡眠
    ESP_LOGI(POWER_TAG, "Going to sleep now");
    esp_deep_sleep_start();
    // 不会执行到这里
}

// 按键唤醒引脚初始化为输入模式
void PowerManager::wakeup_gpio_init()
{
// 配置按键唤醒引脚

#ifdef OUTSIDE
    pinMode(LORA_WAKE_PIN, INPUT);
#endif
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}







