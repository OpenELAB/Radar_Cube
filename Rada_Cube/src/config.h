// 这里定义应用层配置参数相关的宏
#ifndef __CONFIG_H__
#define __CONFIG_H__

// 定义LED的频率
#define LED_FREQ       1000
// LED的引脚电平
#define LED_ACTIVE_LEVEL        1   // High
#define LED_INACTIVE_LEVEL      0   // Low

// 定义蜂鸣器的频率和占空比
#define BEEPER_FREQ    1800
#define BEEPER_DUTY    4

// 定义电池电量的阈值
#define BATTERY_LOW_THRESHOLD       3.0
#define BATTERY_HIGH_THRESHOLD      4.2

// 定义loraAT指令的超时时间和重试次数
#define LORA_AT_TIMEOUT             2000
#define LORA_AT_RETRY               3

// 定义唤醒GPIO引脚的高低电平
#define GPIO_ACTIVE_LEVEL       1   // High
#define GPIO_INACTIVE_LEVEL     0   // Low

#define GPIO_CE_ACTIVE_LEVEL       1   // High
#define GPIO_CE_INACTIVE_LEVEL     0   // Low



#endif
