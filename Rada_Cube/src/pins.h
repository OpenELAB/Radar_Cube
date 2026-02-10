// 这里定义硬件相关，主要是gpio引脚的宏定义
#ifndef __PINS_H__
#define __PINS_H__

#define INSIDE
// #define OUTSIDE

// Lora模块和雷达模块的串口
extern HardwareSerial& LoraSerial;
extern HardwareSerial& RadarSerial;


// 车内模块
#ifdef  INSIDE
    #define GPIO_ACTIVE_LEVEL       1   // High
    #define GPIO_INACTIVE_LEVEL     0   // Low
#endif

// 车外模块
#ifdef  OUTSIDE
    #define GPIO_ACTIVE_LEVEL       0   // Low
    #define GPIO_INACTIVE_LEVEL     1   // High
#endif


// 定义车内模块的引脚
#ifdef INSIDE
    // LED引脚
    #define LED_PIN                 GPIO_NUM_10
    // 蜂鸣器引脚
    #define BEEPER_PIN              GPIO_NUM_4
    // 电池电压采样引脚
    #define BATTERY_PIN             GPIO_NUM_3
    // 唤醒按键引脚
    #define USER_BUTTON_PIN         GPIO_NUM_5  
    #define DEV_BUTTON_PIN          GPIO_NUM_1
    // LORA模块相应引脚
    #define LORA_RX_PIN             GPIO_NUM_6
    #define LORA_TX_PIN             GPIO_NUM_7
    #define LORA_CE_PIN             GPIO_NUM_0
#endif


// 定义车外模块的引脚
#ifdef OUTSIDE
    // LED引脚
    #define LED_PIN                 GPIO_NUM_21
    // 电池电压采样引脚
    #define BATTERY_PIN             GPIO_NUM_1
    // Lora模块相应引脚
    #define LORA_RX_PIN             GPIO_NUM_10
    #define LORA_TX_PIN             GPIO_NUM_11
    #define LORA_CE_PIN             GPIO_NUM_3
    #define LORA_POWER_PIN          GPIO_NUM_5
    // 雷达传感器相应引脚
    #define RADAR_RX_PIN            GPIO_NUM_18
    #define RADAR_TX_PIN            GPIO_NUM_19
    #define RADAR_POWER_PIN         GPIO_NUM_6
    #define RADAR_TRIGGER_PIN       GPIO_NUM_20
    // 唤醒相应引脚
    #define LORA_WAKE_PIN           GPIO_NUM_4
    #define DEV_BUTTON_PIN          GPIO_NUM_0
    #define USER_BUTTON_PIN         GPIO_NUM_2
    #define BUTTON_WAKEUP_BITMASK         ((1ULL << USER_BUTTON_PIN) | (1ULL << DEV_BUTTON_PIN))
#endif

#endif