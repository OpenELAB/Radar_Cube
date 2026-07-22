// 硬件引脚定义 & 硬件相关常量
// 通过 INSIDE / OUTSIDE 宏来区分车内模块和车外模块
// 在 platformio.ini 的 build_flags 中定义，不需要手动修改
#ifndef __PINS_H__
#define __PINS_H__

// // 编译检查：必须且只能定义 INSIDE 或 OUTSIDE 其中一个

// ======================== 串口声明 ========================

// ============================================================
//  车内模块 (INSIDE) 引脚定义
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

// ======================== 按键电平 ========================
#define BUTTON_PRESSED              HIGH
#define BUTTON_RELEASED             LOW

#endif
