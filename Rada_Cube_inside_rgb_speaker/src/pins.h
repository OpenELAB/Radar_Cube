// 硬件引脚定义 & 硬件相关常量
// 通过 INSIDE / OUTSIDE 宏来区分车内模块和车外模块
// 在 platformio.ini 的 build_flags 中定义，不需要手动修改
#ifndef __PINS_H__
#define __PINS_H__

// // 编译检查：必须且只能定义 INSIDE 或 OUTSIDE 其中一个

// ======================== 串口声明 ========================
extern HardwareSerial& LoraSerial;

// ============================================================
//  车内模块 (INSIDE) 引脚定义
// ============================================================
    // #define LED_PIN                 GPIO_NUM_10
    // #define BEEPER_PIN              GPIO_NUM_4
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


// ======================== Lora 电平 ========================
#define LORA_POWER_ON               HIGH
#define LORA_POWER_OFF              LOW
#define LORA_CE_ACTIVE              HIGH    // CE 拉高 = 正常工作
#define LORA_CE_INACTIVE            LOW     // CE 拉低 = 进入配置模式
#define LORA_WAKE_ACTIVE            HIGH    // WAKE 拉高 = 有数据，唤醒 MCU
#define LORA_WAKE_INACTIVE          LOW     // WAKE 拉低 = 空闲

// ======================== 按键电平 ========================
#define BUTTON_PRESSED              HIGH
#define BUTTON_RELEASED             LOW

#endif