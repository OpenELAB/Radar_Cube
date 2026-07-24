#ifndef XIAO_C3_APP_CONFIG_H
#define XIAO_C3_APP_CONFIG_H

#include <Arduino.h>

#ifndef STANDBY_SCAN_INTERVAL_MS
#define STANDBY_SCAN_INTERVAL_MS 3000
#endif

#ifndef STANDBY_SCAN_WINDOW_MS
#define STANDBY_SCAN_WINDOW_MS 30
#endif

#ifndef POWER_TEST_LOG_ENABLED
#define POWER_TEST_LOG_ENABLED 1
#endif

// A fresh board waits for the existing inside unit's ESP-NOW pairing request.
constexpr uint32_t AUTO_PAIR_TIMEOUT_MS = 60000;
constexpr uint32_t PAIR_ACK_REPEAT_COUNT = 3;
constexpr uint32_t PAIR_ACK_GAP_MS = 30;

// The radar-free harness sends a safe, fixed sample long enough for the inside
// unit to verify both links, then requests the existing standby flow.
constexpr uint32_t SIMULATED_WORK_MS = 3000;
constexpr uint32_t SIMULATED_RADAR_INTERVAL_MS = 200;
constexpr uint16_t SIMULATED_DISTANCE_CM = 120;
constexpr int16_t SIMULATED_ANGLE_CENTIDEG = 0;

constexpr uint32_t WAKE_ACK_PERIOD_MS = 200;
constexpr uint32_t WAKE_CONFIRM_TIMEOUT_TEST_MS = 5000;
constexpr uint32_t STANDBY_GRACE_TEST_MS = 500;

#endif
