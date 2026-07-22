// 应用层配置参数
// 这里只放「和硬件引脚无关」的软件参数，引脚定义统一在 pins.h
#ifndef __CONFIG_H__
#define __CONFIG_H__

// ======================== LED ========================
#define LED_FREQ                1000    // PWM 频率 (Hz)
#define LED_ACTIVE_LEVEL        1       // 点亮电平
#define LED_INACTIVE_LEVEL      0       // 熄灭电平

// ======================== 电池 ========================
#define BATTERY_LOW_THRESHOLD   3.0f    // 电池最低电压 (V)
#define BATTERY_HIGH_THRESHOLD  4.2f    // 电池最高电压 (V)

// ======================== 工作模式 ========================
#define WAKE_POLL_INTERVAL_MS   100     // 唤醒轮询间隔 (ms)
#define WAKE_POLL_ROUNDS        30      // 每次发唤醒帧后检查次数（总等 WAKE_POLL_ROUNDS * WAKE_POLL_INTERVAL_MS）
#define WAKE_MAX_RETRY          10      // 唤醒最大重试次数（超过则放弃）
#define WORK_POLL_INTERVAL_MS   50      // 工作循环轮询间隔 (ms)
#define RADAR_SEND_INTERVAL_MS  200     // 雷达数据发送间隔 (ms)
#define WORK_TIMEOUT_MS         (60000 * 3) // 工作模式超时 (ms)，超时自动退出 (目前设为 3 分钟)
#define END_SEND_COUNT          3       // 结束帧重发次数（确保对方收到）
#define STANDBY_ACK_TIMEOUT_MS  1200
#define STANDBY_MAX_RETRY       3
#define STANDBY_RETRY_INTERVAL_MS 200
#define STANDBY_ACK_GRACE_MS    300
#define WAKE_ACK_INTERVAL_MS    200
#define WAKE_CONFIRM_TIMEOUT_MS 5000

// ======================== BLE wake ========================
#define BLE_WAKE_ADV_INTERVAL_MIN   32    // 20 ms, unit: 0.625 ms
#define BLE_WAKE_ADV_INTERVAL_MAX   32    // 20 ms, unit: 0.625 ms
#define BLE_SCAN_INTERVAL_MS        60
#define BLE_SCAN_WINDOW_MS          60
#define BLE_SCAN_BURST_MS           60
#define OUTSIDE_DEEP_SLEEP_MS       2900
#define OUTSIDE_SLEEP_JITTER_MS     200

// ======================== 按键 ========================
#define BUTTON_LONG_PRESS_MS    3000    // 长按阈值 (ms)，>3s 判定为长按

// 距离报警阈值 (cm)
#define DIST_DANGER_CM          30      // <= 30 cm
#define DIST_CLOSE_CM           60      // 31..60 cm
#define DIST_MID_CM             90      // 61..90 cm
#define DIST_VERY_FAR_CM        120     // 91..120 cm
#define DIST_FAR_CM             150     // 121..150 cm；> 150 cm 为安全距离
// 迟滞区间大小
#define DIST_HYSTERESIS_CM      5

// 雷达测距的阈值
#define DIST_MIN_CM             20
#define DIST_MAX_CM             250

// ======================== 日志标签 ========================
// 每个模块一个标签，用于 ESP_LOGI / ESP_LOGE 的 tag 参数
#define MAIN_TAG                "MAIN"
#define POWER_TAG               "POWER"
#define BLE_WAKE_TAG            "BLE_WAKE"
#define MAC_TAG                 "MAC"
#define RADAR_TAG               "RADAR"
#define SLAVE_A_TAG             "SLAVE_A"
#define SLAVE_B_TAG             "SLAVE_B"

// ======================== 从机id ========================
#define SLAVE_A_ID              0
#define SLAVE_B_ID              1


// 日志输出标签
#define POWER_TAG         "POWER"
#define MAC_TAG            "MAC"


#endif
