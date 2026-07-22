#include "sensor.h"

#include <driver/gpio.h>
#include <esp_sleep.h>

PowerManager* PowerManager::_instance = nullptr;

void LEDControler::led_init()
{
    pinMode(LED_PIN, OUTPUT);
    led_off();
}

void LEDControler::led_on() { digitalWrite(LED_PIN, LED_ACTIVE_LEVEL); }
void LEDControler::led_off() { digitalWrite(LED_PIN, LED_INACTIVE_LEVEL); }

void LEDControler::blink(LED_PERIOD period)
{
    pinMode(LED_PIN, OUTPUT);
    const uint32_t half_period = period / 2;
    led_on();
    vTaskDelay(pdMS_TO_TICKS(half_period));
    led_off();
    vTaskDelay(pdMS_TO_TICKS(half_period));
}

void LEDControler::breath(LED_SPEED speed)
{
    ledcAttach(LED_PIN, LED_FREQ, LEDC_TIMER_8_BIT);
    for (int duty = 0; duty < 255; duty += 2) {
        ledcWrite(LED_PIN, duty);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
    for (int duty = 255; duty >= 0; duty -= 2) {
        ledcWrite(LED_PIN, duty);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
    ledcDetach(LED_PIN);
}

void PowerManager::power_init()
{
    pinMode(BATTERY_PIN, INPUT);
}

void PowerManager::prepareSafePins()
{
    // GPIO holds survive deep sleep on ESP32-C6. Release them before changing
    // the restored pin configuration, then force every high-current load off.
    gpio_hold_dis(static_cast<gpio_num_t>(RADAR_POWER_PIN));
    gpio_hold_dis(static_cast<gpio_num_t>(LED_PIN));

    pinMode(RADAR_POWER_PIN, OUTPUT);
    digitalWrite(RADAR_POWER_PIN, RADAR_POWER_OFF);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_INACTIVE_LEVEL);

    pinMode(RADAR_RX_PIN, INPUT);
    pinMode(RADAR_TX_PIN, INPUT);
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}

uint8_t PowerManager::get_battery_value()
{
    uint32_t sum_mv = 0;
    for (int i = 0; i < 32; ++i) sum_mv += analogReadMilliVolts(BATTERY_PIN);

    float voltage = (sum_mv * 2) / 32.0f / 1000.0f;
    voltage = constrain(voltage, BATTERY_LOW_THRESHOLD, BATTERY_HIGH_THRESHOLD);
    const uint8_t percent = static_cast<uint8_t>(
        (voltage - BATTERY_LOW_THRESHOLD) * 100.0f /
        (BATTERY_HIGH_THRESHOLD - BATTERY_LOW_THRESHOLD) + 0.5f);
    ESP_LOGI(POWER_TAG, "battery voltage=%.3f V, percentage=%u%%",
             voltage, percent);
    return percent;
}

WakeupSource PowerManager::getWakeupSource()
{
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) return WAKEUP_TIMER;
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        _gpio_wakeup_status = esp_sleep_get_gpio_wakeup_status();
        return classifyWakeButtons();
    }
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) return WAKEUP_POWER_ON;
    return WAKEUP_UNKNOWN;
}

WakeupSource PowerManager::classifyWakeButtons()
{
    // Capture a nearly simultaneous second button and tolerate contact bounce.
    vTaskDelay(pdMS_TO_TICKS(30));
    const bool user = digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
                      (_gpio_wakeup_status & BIT64(USER_BUTTON_PIN)) ||
                      (_runtime_button_status & BIT(USER_BUTTON_PIN));
    const bool dev = digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED ||
                     (_gpio_wakeup_status & BIT64(DEV_BUTTON_PIN)) ||
                     (_runtime_button_status & BIT(DEV_BUTTON_PIN));
    _button_activity_observed = _button_activity_observed || user || dev;
    if (user && dev) return WAKEUP_BOTH_BUTTONS;
    if (user) return WAKEUP_USER_BUTTON;
    if (dev) return WAKEUP_DEV_BUTTON;
    return WAKEUP_UNKNOWN;
}

void PowerManager::beginButtonMonitoring()
{
    _instance = this;
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
    attachInterrupt(USER_BUTTON_PIN, userButtonIsr, RISING);
    attachInterrupt(DEV_BUTTON_PIN, devButtonIsr, RISING);
}

void ARDUINO_ISR_ATTR PowerManager::userButtonIsr()
{
    if (_instance) {
        _instance->_runtime_button_status |= BIT(USER_BUTTON_PIN);
        _instance->_button_activity_observed = true;
    }
}

void ARDUINO_ISR_ATTR PowerManager::devButtonIsr()
{
    if (_instance) {
        _instance->_runtime_button_status |= BIT(DEV_BUTTON_PIN);
        _instance->_button_activity_observed = true;
    }
}

void PowerManager::waitForButtonsReleased()
{
    while (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
           digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(30));
}

[[noreturn]] void PowerManager::enterDeepSleep(uint32_t sleep_ms,
                                               bool enable_timer)
{
    // A pure RTC wake normally has no button activity, so do not spend a fixed
    // 30 ms awake on release debounce. Button-related paths retain the full
    // release wait and debounce through this sticky activity flag.
    const bool button_guard_required =
        _button_activity_observed ||
        buttonEventPending() ||
        digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
        digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED;
    if (button_guard_required) waitForButtonsReleased();

    detachInterrupt(USER_BUTTON_PIN);
    detachInterrupt(DEV_BUTTON_PIN);
    clearButtonEvents();

    RadarSerial.end();
    prepareSafePins();

    // Keep the active-high load enables low while the digital GPIO domain is off.
    gpio_hold_en(static_cast<gpio_num_t>(RADAR_POWER_PIN));
    gpio_hold_en(static_cast<gpio_num_t>(LED_PIN));

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const uint64_t button_mask = BIT64(USER_BUTTON_PIN) | BIT64(DEV_BUTTON_PIN);
    const esp_err_t gpio_err = esp_deep_sleep_enable_gpio_wakeup(
        button_mask, ESP_GPIO_WAKEUP_GPIO_HIGH);
    if (gpio_err != ESP_OK) {
        ESP_LOGE(POWER_TAG, "Deep-sleep button wake setup failed: %d", gpio_err);
    }

    if (enable_timer) {
        esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_ms) * 1000ULL);
    }
    ESP_LOGI(POWER_TAG, "Deep-sleep: timer=%s, interval=%lu ms",
             enable_timer ? "on" : "off",
             static_cast<unsigned long>(sleep_ms));
    Serial.flush();
    esp_deep_sleep_start();
    __builtin_unreachable();
}
