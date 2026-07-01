#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "config.h"
#include "pins.h"

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

// 电池状态
enum BATTERY_STAT
{
    NORMAL,
    LOW_ENGERGY,
    NO_ENERGY
};

// 唤醒源（统一枚举，不区分 INSIDE/OUTSIDE）
enum WakeupSource {
    WAKEUP_POWER_ON,        // 首次上电 / 复位
    WAKEUP_USER_BUTTON,     // 用户按键唤醒
    WAKEUP_DEV_BUTTON,      // 开发按键唤醒
    WAKEUP_BOTH_BUTTONS,    // 两个按键同时按下（取决于硬件接线，可能出现）
    WAKEUP_LORA,            // Lora 唤醒（仅 OUTSIDE）
};

/**
 * LED 控制类
 *
 * 使用示例：
 *   LEDControler Led;
 *   Led.led_init();
 *   Led.blink(LED_PERIOD_1);   // 2s 周期闪烁
 *   Led.breath(LED_SPEED_1);   // 呼吸灯效果
 *   Led.led_off();
 */
class LEDControler
{
public:
    void led_init();
    void led_on();
    void led_off();
    void blink(LED_PERIOD period);
    void breath(LED_SPEED speed);
};

/**
 * 蜂鸣器控制类（仅 INSIDE 模块使用）
 *
 * 使用示例：
 *   BeeperControler Beeper;
 *   Beeper.beeper_init();
 *   Beeper.beep(BEEPER_PERIOD_2);  // 0.5s 周期蜂鸣
 *   Beeper.beep_stop();            // 停止蜂鸣
 */
// class BeeperControler
// {
// public:
//     void beeper_init();
//     void beep();
//     void beep_stop();
//     void play_success_tone(); // 添加成功提示音方法
//     void play_fail_tone();    // 添加失败提示音方法

//     // 查蜂鸣器周期表
//     uint16_t get_period(uint8_t mode);

    
// private:
//     static const uint16_t PERIODS[]; // 定义周期数组
// };

/**
 * 电池管理 & 睡眠控制
 *
 * 使用示例：
 *   PowerManager Power;
 *   Power.wakeup_gpio_init();      // 初始化唤醒引脚
 *   Power.get_wakeup_reason();     // 获取唤醒原因
 *   Power.power_init();            // 初始化电池采集
 *   uint8_t bat = Power.get_battery_value();  // 0-100
 *   Power.deep_sleep();            // 进入深度睡眠
 */
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
    // 检测唤醒原因（ESP-IDF API + 读按钮电平），结果存入 _wakeup_src
    void detectWakeupSource();
    // 获取检测到的唤醒源
    WakeupSource getWakeupSource() const { return _wakeup_src; }
    // 获取电池电压值
    uint8_t get_battery_value();

private:
    WakeupSource _wakeup_src = WAKEUP_POWER_ON;
};

#endif
