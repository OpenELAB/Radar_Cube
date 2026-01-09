// 这里定义硬件相关，主要是gpio引脚的宏
#ifndef __PINS_H__
#define __PINS_H__

#define INSIDE
// #define OUTSIDE

// 定义车内模块引脚
#ifdef INSIDE
    #define BEEPER_PIN
    #define LED_PIN
    #define LORA_TX_PIN
    #define LORA_RX_PIN
    #define BUTTON_PIN
    #define BATTERY_PIN
#endif
// 定义车外模块引脚
#ifdef OUTSIDE
    #define LED_PIN
    #define LORA_TX_PIN
    #define LORA_RX_PIN
    #define BUTTON_PIN
    #define BATTERY_PIN
    
#endif





#endif
