// Application layer configuration parameters
// Only software parameters that are “independent of hardware pins” are placed here. The pin definitions are unified in pins.h
#ifndef __CONFIG_H__
#define __CONFIG_H__

// // ======================== LED ========================
// #define LED_FREQ 1000 // PWM frequency (Hz)
// #define LED_ACTIVE_LEVEL 1 // Lighting level
// #define LED_INACTIVE_LEVEL 0 // Turn off level

// // ======================== Buzzer ========================
// #define BEEPER_FREQ 1800 // PWM frequency (Hz)
// #define BEEPER_DUTY 4 // PWM duty cycle

// ======================== Battery ========================
#define BATTERY_LOW_THRESHOLD   3.0f    // Minimum battery voltage (V)
#define BATTERY_HIGH_THRESHOLD  4.2f    // Maximum battery voltage (V)

// ======================== Lora ========================
#define LORA_AT_TIMEOUT         2000    // AT command timeout (ms)
#define LORA_AT_RETRY           3       // AT command retry times

// ======================== Working mode ========================
#define WAKE_POLL_INTERVAL_MS   100     // Wake-up polling interval (ms)
#define WAKE_POLL_ROUNDS        30      // Number of checks after each wake-up frame is sent (total wait WAKE_POLL_ROUNDS * WAKE_POLL_INTERVAL_MS)
#define WAKE_MAX_RETRY          10      // Maximum number of retries to wake up (give up if exceeded)
#define WORK_POLL_INTERVAL_MS   50      // Work cycle polling interval (ms)
#define RADAR_SEND_INTERVAL_MS  200     // Radar data sending interval (ms)
#define WORK_TIMEOUT_MS         (60000 * 5) // Working mode timeout (ms), automatic exit after timeout (currently set to 5 minutes)
#define END_SEND_COUNT          3       // Number of end frame retransmissions (make sure the other party receives it)

// ======================== Buttons ========================
#define BUTTON_LONG_PRESS_MS    3000    // Long press threshold (ms), >3s is considered a long press

// Distance alarm threshold (mm)
// #define DIST_DANGER_CM 70 // < 0.7m emergency alarm
// #define DIST_CLOSE_CM 120 // < 0.5m urgent alarm
// #define DIST_MID_CM 170 // < 1.5m medium speed alarm
// #define DIST_FAR_CM 220 // < 2.2m slow alarm
// Hysteresis interval size
#define DIST_DANGER_CM          70
#define DIST_CLOSE_CM           120
#define DIST_MID_CM             170
#define DIST_FAR_CM             220
#define DIST_HYSTERESIS_CM      5

// Threshold of radar ranging
#define DIST_MIN_CM             20
#define DIST_MAX_CM             250
#define DIST_DATA_TIMEOUT_MS    (RADAR_SEND_INTERVAL_MS * 4)

// ======================== Log tag ========================
// One tag per module, used for the tag parameter of ESP_LOGI / ESP_LOGE
#define MAIN_TAG                "MAIN"
#define POWER_TAG               "POWER"
#define LORA_TAG                "LORA"
#define MAC_TAG                 "MAC"
#define RADAR_TAG               "RADAR"
#define SLAVE_A_TAG             "SLAVE_A"
#define SLAVE_B_TAG             "SLAVE_B"

// ======================== Slave ID ========================
#define SLAVE_A_ID              0
#define SLAVE_B_ID              1


#define GPIO_CE_ACTIVE_LEVEL       1   // High
#define GPIO_CE_INACTIVE_LEVEL     0   // Low

// Log output label
#define POWER_TAG         "POWER"
#define LORA_TAG          "LORA"
#define MAC_TAG            "MAC"


#endif
