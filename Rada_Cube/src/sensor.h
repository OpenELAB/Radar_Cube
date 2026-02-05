#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "config.h"

// LED频闪周期
enum LED_PERIOD
{
    LED_PERIOD_1 = 2000, // 频闪周期2s
    LED_PERIOD_2 = 1000, // 频闪周期1s
    LED_PERIOD_3 = 500, // 频闪周期0.5s
};

// LED呼吸灯周期
enum LED_SPEED
{
    LED_SPEED_1 = 10, // 呼吸周期 2.56s
    LED_SPEED_2 = 15, // 呼吸周期 3.84s
    LED_SPEED_3 = 20, // 呼吸周期 5.12s
};

// 蜂鸣器鸣叫周期
enum BEEP
{
    BEEPER_PERIOD_1 = 1000, // 蜂鸣周期1s
    BEEPER_PERIOD_2 = 500,  // 蜂鸣周期0.5s
    BEEPER_PERIOD_4 = 250,  // 蜂鸣周期0.25s
    BEEPER_PERIOD_LONG = 0  // 长鸣
};

// 电池状态
enum BATTERY_STAT
{
    NORMAL,
    LOW_ENGERGY,
    NO_ENERGY
};

// 定义LED类
class LEDControler
{
public:
    void led_init();
    void blink(LED_PERIOD period);
    void breath(LED_SPEED speed);
};

// 定义蜂鸣器类
class BeeperControler
{
public:
    void beeper_init();
    void beep(BEEP type);
    void beep_stop();
};

// 定义电池管理类
class PowerManager
{
public:
    // 初始化电池电压采集引脚
    void power_init();
    // 等待唤醒引脚电平复位，进入睡眠模式
    void deep_sleep();
    // 唤醒引脚初始化
    void wakeup_gpio_init();
    // 等待唤醒引脚电平复位
    void wait_wakeup_button_intend();
    // 获取唤醒原因
    void get_wakeup_reason();
    // 获取电池电压值
    uint8_t get_battery_value();
    // 用于按键扫描
    void wake_button_detection();

    // ================================== 第一版LORA功率开关控制引脚需要持续拉高 ==================================
    void lora_power_keep_high();


private:
    bool user_button_level = GPIO_ACTIVE_LEVEL;
    bool dev_button_level = GPIO_ACTIVE_LEVEL;
};

#endif
