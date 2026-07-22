#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "config.h"
#include "pins.h"


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
};


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
