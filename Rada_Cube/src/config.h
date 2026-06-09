// 应用层配置参数
// 这里只放「和硬件引脚无关」的软件参数，引脚定义统一在 pins.h
#ifndef __CONFIG_H__
#define __CONFIG_H__

// ======================== LED ========================
#define LED_FREQ                1000    // PWM 频率 (Hz)
#define LED_ACTIVE_LEVEL        1       // 点亮电平
#define LED_INACTIVE_LEVEL      0       // 熄灭电平

// ======================== 蜂鸣器 ========================
#define BEEPER_FREQ             1800    // PWM 频率 (Hz)
#define BEEPER_DUTY             4       // PWM 占空比

// ======================== 电池 ========================
#define BATTERY_LOW_THRESHOLD   3.0f    // 电池最低电压 (V)
#define BATTERY_HIGH_THRESHOLD  4.2f    // 电池最高电压 (V)

// ======================== Lora ========================
#define LORA_AT_TIMEOUT         2000    // AT 指令超时 (ms)
#define LORA_AT_RETRY           3       // AT 指令重试次数

// ======================== 工作模式 ========================
#define WAKE_POLL_INTERVAL_MS   100     // 唤醒轮询间隔 (ms)
#define WAKE_POLL_ROUNDS        30      // 每次发唤醒帧后检查次数（总等 WAKE_POLL_ROUNDS * WAKE_POLL_INTERVAL_MS）
#define WAKE_MAX_RETRY          10      // 唤醒最大重试次数（超过则放弃）
#define WORK_POLL_INTERVAL_MS   50      // 工作循环轮询间隔 (ms)
#define RADAR_SEND_INTERVAL_MS  200     // 雷达数据发送间隔 (ms)
#define WORK_TIMEOUT_MS         (60000 * 5) // 工作模式超时 (ms)，超时自动退出 (目前设为 5 分钟)
#define END_SEND_COUNT          3       // 结束帧重发次数（确保对方收到）

// ======================== 按键 ========================
#define BUTTON_LONG_PRESS_MS    3000    // 长按阈值 (ms)，>3s 判定为长按

// 距离报警阈值 (mm)
#define DIST_DANGER_CM          70      // < 0.7m  紧急报警
#define DIST_CLOSE_CM           120     // < 0.5m  急促报警
#define DIST_MID_CM             170    // < 1.5m  中速报警
#define DIST_FAR_CM             220    // < 2.2m  慢速报警
// 迟滞区间大小
#define DIST_HYSTERESIS_CM      5

// 雷达测距的阈值
#define DIST_MIN_CM             20
#define DIST_MAX_CM             250

// ======================== 日志标签 ========================
// 每个模块一个标签，用于 ESP_LOGI / ESP_LOGE 的 tag 参数
#define MAIN_TAG                "MAIN"
#define POWER_TAG               "POWER"
#define LORA_TAG                "LORA"
#define MAC_TAG                 "MAC"
#define RADAR_TAG               "RADAR"
#define SLAVE_A_TAG             "SLAVE_A"
#define SLAVE_B_TAG             "SLAVE_B"

// ======================== 从机id ========================
#define SLAVE_A_ID              0
#define SLAVE_B_ID              1



#define GPIO_CE_ACTIVE_LEVEL       1   // High
#define GPIO_CE_INACTIVE_LEVEL     0   // Low


// 日志输出标签
#define POWER_TAG         "POWER"
#define LORA_TAG          "LORA"
#define MAC_TAG            "MAC"



#endif
