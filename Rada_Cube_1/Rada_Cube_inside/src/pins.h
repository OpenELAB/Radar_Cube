// Hardware pin definition & hardware related constants
// Distinguish between in-car modules and out-of-vehicle modules through INSIDE / OUTSIDE macros
// Defined in build_flags of platformio.ini, no manual modification is required.
#ifndef __PINS_H__
#define __PINS_H__

// // Compilation check: must and can only define one of INSIDE or OUTSIDE

// ======================== Serial port declaration ========================
extern HardwareSerial& LoraSerial;

// ============================================================
//  In-vehicle module (INSIDE) pin definition
// ============================================================
#define RGB_LED_PIN             GPIO_NUM_4
#define RGB_LED_PWR_PIN         GPIO_NUM_2
#define SPEAKER_I2S_LRC_PIN     GPIO_NUM_19
#define SPEAKER_I2S_BCLK_PIN    GPIO_NUM_20
#define SPEAKER_I2S_DIN_PIN     GPIO_NUM_21
#define SPEAKER_SHUTDOWN_PIN    GPIO_NUM_7
#define BATTERY_PIN             GPIO_NUM_3
#define USER_BUTTON_PIN         GPIO_NUM_6
#define DEV_BUTTON_PIN          GPIO_NUM_1
#define LORA_RX_PIN             GPIO_NUM_10
#define LORA_TX_PIN             GPIO_NUM_11
#define LORA_CE_PIN             GPIO_NUM_0
#define LORA_POWER_PIN          GPIO_NUM_18


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

#endif