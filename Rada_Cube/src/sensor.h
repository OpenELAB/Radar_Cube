#ifndef __SENSOR_H__
#define __SENSOR_H__

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
    void init();
    void blink(LED_PERIOD period);
    void breath(LED_SPEED speed);
};

// 定义蜂鸣器类
class BeeperControler
{
public:
    void init();
    void beep(BEEP type);
    void beep_stop();
};

// 定义电池管理类
class PowerManager
{
public:
    void init();
    esp_err_t deep_sleep();
    void wait_wakeup_button_intend();
    void get_wakeup_reason();
    uint8_t get_battery_value();
};

#endif
