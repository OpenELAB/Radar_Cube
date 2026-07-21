// Here you define hardware-related macros, mainly gpio pin macros
#ifndef __PINS_H__
#define __PINS_H__

#define INSIDE
// #define OUTSIDE

// Define the in-car module pins
#ifdef INSIDE
    #define BEEPER_PIN
    #define LED_PIN
    #define LORA_TX_PIN
    #define LORA_RX_PIN
    #define BUTTON_PIN
    #define BATTERY_PIN
#endif
// Define the external module pins
#ifdef OUTSIDE
    #define LED_PIN
    #define LORA_TX_PIN
    #define LORA_RX_PIN
    #define BUTTON_PIN
    #define BATTERY_PIN
    
#endif





#endif
