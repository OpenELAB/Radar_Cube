// 这里定义硬件相关，主要是gpio引脚的宏定义
#ifndef __PINS_H__
#define __PINS_H__

// #define INSIDE
#define OUTSIDE

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


// Lora模块和雷达模块的串口
extern HardwareSerial& LoraSerial;
extern HardwareSerial& RadarSerial;




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
    #define LORA_POWER_PIN          GPIO_NUM_18
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




// Lora相关
// Lora开关
#define LORA_POWER_OFF             LOW
#define LORA_POWER_ON              HIGH
// Lora的CE控制引脚
#define GPIO_CE_ACTIVE_LEVEL       HIGH   
#define GPIO_CE_INACTIVE_LEVEL     LOW   


// 按键相关
// 按键的状态
#define USER_BUTTON_PRESSED             HIGH
#define DEV_BUTTON_PRESSED              HIGH
#define USER_BUTTON_RELEASED            LOW
#define DEV_BUTTON_RELEASED             LOW


// 按键的电平
#define USER_BUTTON_ACTIVE_LEVEL        HIGH
#define DEV_BUTTON_ACTIVE_LEVEL         HIGH
#define USER_BUTTON_INACTIVE_LEVEL      LOW
#define DEV_BUTTON_INACTIVE_LEVEL       LOW


// 雷达相关
#define RADAR_POWER_OFF                 LOW
#define RADAR_POWER_ON                  HIGH


// MAC地址相关
#define MASTER_MAC_ADDR_EXIST           true
#define SLAVE_MAC_ADDR_EXIST            true
#define MASTER_MAC_ADDR_NOT_EXIST       false
#define SLAVE_MAC_ADDR_NOT_EXIST        false





#endif