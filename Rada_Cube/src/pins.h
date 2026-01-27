// 这里定义硬件相关，主要是gpio引脚的宏定义
#ifndef __PINS_H__
#define __PINS_H__

#define INSIDE
// #define OUTSIDE

// 定义车内模块的引脚
#ifdef INSIDE
    #define LED_PIN                 GPIO_NUM_10
    #define BEEPER_PIN              GPIO_NUM_4
    #define BATTERY_PIN             GPIO_NUM_3
    #define WAKE_BUTTON_PIN         GPIO_NUM_5  
    #define DEV_BUTTON_PIN          GPIO_NUM_1
    #define LORA_RX_PIN             GPIO_NUM_6
    #define LORA_TX_PIN             GPIO_NUM_7
    #define LORA_CE_PIN             GPIO_NUM_0
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




#endif