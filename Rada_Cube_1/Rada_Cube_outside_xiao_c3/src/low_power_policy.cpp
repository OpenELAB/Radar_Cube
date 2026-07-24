#include "low_power_policy.h"

#include <esp_err.h>
#include <esp_pm.h>
#include <esp_wifi.h>

bool LowPowerPolicy::logsEnabled_ = false;

namespace {

void logResult(bool enabled, const char* operation, esp_err_t result) {
#if POWER_TEST_LOG_ENABLED
  if (enabled) {
    Serial.printf("[power] %s: %s (0x%x)\r\n", operation,
                  esp_err_to_name(result), static_cast<unsigned>(result));
  }
#else
  (void)enabled;
  (void)operation;
  (void)result;
#endif
}

bool isWiFiInactiveResult(esp_err_t result) {
  return result == ESP_OK || result == ESP_ERR_WIFI_NOT_INIT ||
         result == ESP_ERR_WIFI_NOT_STARTED;
}

}  // namespace

bool LowPowerPolicy::begin(bool enableLogs) {
  logsEnabled_ = enableLogs;

  esp_pm_config_t config{};
  config.max_freq_mhz = 160;
  config.min_freq_mhz = 40;
  config.light_sleep_enable = true;

  const esp_err_t result = esp_pm_configure(&config);
  logResult(logsEnabled_, "esp_pm_configure", result);

#if POWER_TEST_LOG_ENABLED
  if (logsEnabled_) {
    Serial.printf("[power] external 32.768 kHz clock: %s\r\n",
                  external32kConfigured() ? "configured" : "NOT configured");
  }
#endif

  return result == ESP_OK && external32kConfigured();
}

bool LowPowerPolicy::enterBleStandby() {
  // esp_wifi_get_mode() gives us a side-effect-free way to tell whether the
  // Wi-Fi driver has ever been initialized.
  wifi_mode_t mode = WIFI_MODE_NULL;
  const esp_err_t modeResult = esp_wifi_get_mode(&mode);
  if (modeResult == ESP_ERR_WIFI_NOT_INIT) {
    return true;
  }
  if (modeResult != ESP_OK) {
    logResult(logsEnabled_, "esp_wifi_get_mode", modeResult);
    return false;
  }

  const esp_err_t stopResult = esp_wifi_stop();
  logResult(logsEnabled_, "esp_wifi_stop", stopResult);
  if (!isWiFiInactiveResult(stopResult)) {
    return false;
  }

  const esp_err_t deinitResult = esp_wifi_deinit();
  logResult(logsEnabled_, "esp_wifi_deinit", deinitResult);
  return deinitResult == ESP_OK || deinitResult == ESP_ERR_WIFI_NOT_INIT;
}

void LowPowerPolicy::idle(TickType_t ticks) {
  if (ticks == 0) {
    taskYIELD();
    return;
  }
  vTaskDelay(ticks);
}

bool LowPowerPolicy::external32kConfigured() {
#if defined(CONFIG_RTC_CLK_SRC_EXT_CRYS) && \
    defined(CONFIG_BT_CTRL_LPCLK_SEL_EXT_32K_XTAL)
  return true;
#else
  return false;
#endif
}

bool LowPowerPolicy::logsEnabled() {
  return logsEnabled_;
}
