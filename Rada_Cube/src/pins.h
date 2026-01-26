// 这里定义硬件相关，主要是gpio引脚的宏定义
#ifndef __PINS_H__
#define __PINS_H__

#define INSIDE
// #define OUTSIDE

// 定义车内模块的引脚
#ifdef INSIDE
    #define LED_PIN                 10
    #define BEEPER_PIN              4
    #define BATTERY_PIN             3
    #define WAKE_BUTTON_PIN         5  
    #define DEV_BUTTON_PIN          1
    #define LORA_RX_PIN             6
    #define LORA_TX_PIN             7
    #define LORA_CE_PIN             0
#endif


// 定义车外模块的引脚
#ifdef OUTSIDE
    #define LED_PIN
    #define BATTERY_PIN
    #define LORA_RX
    #define LORA_TX
    #define LORA_CE
    #define DEBUG_RX
    #define DEBUG_TX
    #define RADA_RX
    #define RADA_TX

#endif

#define LED_FREQ       1000

#define BEEPER_FREQ    1800
#define BEEPER_DUTY    4


#endif