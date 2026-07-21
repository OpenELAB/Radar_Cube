// Hardware pin definition & hardware related constants
// Distinguish between in-car modules and out-of-vehicle modules through INSIDE / OUTSIDE macros
// Defined in build_flags of platformio.ini, no manual modification is required.
#ifndef __PINS_H__
#define __PINS_H__

// Compilation check: must and can only define one of INSIDE or OUTSIDE
// #if !defined(INSIDE) && !defined(OUTSIDE)
//     #error "Please define INSIDE or OUTSIDE in platformio.ini build_flags"
// #endif
// #if defined(INSIDE) && defined(OUTSIDE)
//     #error "Cannot define both INSIDE and OUTSIDE"
// #endif

// ======================== Serial port declaration ========================
extern HardwareSerial& LoraSerial;
// #ifdef OUTSIDE
extern HardwareSerial& RadarSerial;
// #endif

// ============================================================
//  Outboard module (OUTSIDE) pin definition
// ============================================================
// #ifdef OUTSIDE
    #define LED_PIN                 GPIO_NUM_21
    #define BATTERY_PIN             GPIO_NUM_1
    #define LORA_RX_PIN             GPIO_NUM_10
    #define LORA_TX_PIN             GPIO_NUM_11
    #define LORA_CE_PIN             GPIO_NUM_3
    #define LORA_POWER_PIN          GPIO_NUM_5
    #define RADAR_RX_PIN            GPIO_NUM_18
    #define RADAR_TX_PIN            GPIO_NUM_19
    #define RADAR_POWER_PIN         GPIO_NUM_6
    #define RADAR_TRIGGER_PIN       GPIO_NUM_20
    #define LORA_WAKE_PIN           GPIO_NUM_4
    #define USER_BUTTON_PIN         GPIO_NUM_2
    #define DEV_BUTTON_PIN          GPIO_NUM_0
    // EXT1 bit mask required for wake-up
    #define BUTTON_WAKEUP_BITMASK   ((1ULL << USER_BUTTON_PIN) | (1ULL << DEV_BUTTON_PIN))
// #endif

// ======================== Lora Level ========================
#define LORA_POWER_ON               HIGH
#define LORA_POWER_OFF              LOW
#define LORA_CE_ACTIVE              HIGH    // CE pulled high = normal operation
#define LORA_CE_INACTIVE            LOW     // CE pulled low = enter configuration mode
#define LORA_WAKE_ACTIVE            HIGH    // WAKE pulled high = there is data, wake up MCU
#define LORA_WAKE_INACTIVE          LOW     // WAKE pulled low = idle

// ======================== Key level ========================
#define BUTTON_PRESSED              HIGH
#define BUTTON_RELEASED             LOW

// ======================== Radar power supply ========================
#define RADAR_POWER_ON              HIGH
#define RADAR_POWER_OFF             LOW

#endif