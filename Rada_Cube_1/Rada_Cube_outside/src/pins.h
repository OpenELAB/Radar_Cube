// 硬件引脚定义 & 硬件相关常量
// 通过 INSIDE / OUTSIDE 宏来区分车内模块和车外模块
// 在 platformio.ini 的 build_flags 中定义，不需要手动修改
#ifndef __PINS_H__
#define __PINS_H__

// 编译检查：必须且只能定义 INSIDE 或 OUTSIDE 其中一个
// #if !defined(INSIDE) && !defined(OUTSIDE)
//     #error "Please define INSIDE or OUTSIDE in platformio.ini build_flags"
// #endif
// #if defined(INSIDE) && defined(OUTSIDE)
//     #error "Cannot define both INSIDE and OUTSIDE"
// #endif

// ======================== 串口声明 ========================
// #ifdef OUTSIDE
extern HardwareSerial& RadarSerial;
// #endif

// ============================================================
//  车外模块 (OUTSIDE) 引脚定义
// ============================================================
// #ifdef OUTSIDE
    #define LED_PIN                 GPIO_NUM_21
    #define BATTERY_PIN             GPIO_NUM_1
    #define RADAR_RX_PIN            GPIO_NUM_18
    #define RADAR_TX_PIN            GPIO_NUM_19
    #define RADAR_POWER_PIN         GPIO_NUM_6
    #define RADAR_TRIGGER_PIN       GPIO_NUM_20
    #define USER_BUTTON_PIN         GPIO_NUM_2
    #define DEV_BUTTON_PIN          GPIO_NUM_0
    // EXT1 唤醒需要的位掩码
    #define BUTTON_WAKEUP_BITMASK   ((1ULL << USER_BUTTON_PIN) | (1ULL << DEV_BUTTON_PIN))
// #endif

// ======================== 按键电平 ========================
#define BUTTON_PRESSED              HIGH
#define BUTTON_RELEASED             LOW

// ======================== 雷达电源 ========================
#define RADAR_POWER_ON              HIGH
#define RADAR_POWER_OFF             LOW

#endif
