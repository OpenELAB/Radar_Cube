#pragma once

#include <Arduino.h>

#ifndef POWER_TEST_LOG_ENABLED
#define POWER_TEST_LOG_ENABLED 0
#endif

// Owns only platform power policy. BLE scanning and ESP-NOW session state stay
// in their respective modules.
class LowPowerPolicy {
 public:
  // Configures dynamic frequency scaling and automatic light sleep.
  // Returns false when PM cannot be configured or this build is not using the
  // external 32.768 kHz RTC/BLE low-power clock.
  static bool begin(bool enableLogs = (POWER_TEST_LOG_ENABLED != 0));

  // Stops and deinitializes Wi-Fi before BLE standby. It is safe to call when
  // Wi-Fi has not been initialized or is already stopped.
  static bool enterBleStandby();

  // Blocks the caller so the FreeRTOS idle task can enter automatic light
  // sleep. BLE controller events still wake and run their own tasks.
  static void idle(TickType_t ticks = portMAX_DELAY);

  static bool external32kConfigured();
  static bool logsEnabled();

 private:
  static bool logsEnabled_;
};
