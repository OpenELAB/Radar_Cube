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

bool PowerManager::wakeup_gpio_init()
{
    if (!_button_queue) _button_queue = xQueueCreate(4, sizeof(uint8_t));
    if (!_button_queue) return false;

    _instance = this;
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
    attachInterrupt(USER_BUTTON_PIN, buttonIsr, RISING);
    attachInterrupt(DEV_BUTTON_PIN, buttonIsr, RISING);

    const esp_err_t user_err = gpio_wakeup_enable(
        static_cast<gpio_num_t>(USER_BUTTON_PIN), GPIO_INTR_HIGH_LEVEL);
    const esp_err_t dev_err = gpio_wakeup_enable(
        static_cast<gpio_num_t>(DEV_BUTTON_PIN), GPIO_INTR_HIGH_LEVEL);
    const esp_err_t wake_err = esp_sleep_enable_gpio_wakeup();
    if (user_err != ESP_OK || dev_err != ESP_OK || wake_err != ESP_OK) {
        ESP_LOGE(POWER_TAG, "Button GPIO wake setup failed: user=%d dev=%d wake=%d",
                 user_err, dev_err, wake_err);
        return false;
    }

    armButtonWakeup();
    return true;
}

void ARDUINO_ISR_ATTR PowerManager::buttonIsr()
{
    PowerManager* instance = _instance;
    if (!instance || !instance->_button_queue || !instance->_button_irq_armed) return;

    // gpio_wakeup_enable() changes these lines to level-triggered interrupts.
    // Latch the first edge/level and disable both IRQs until both buttons have
    // been released, otherwise a held HIGH level continuously re-enters here.
    instance->_button_irq_armed = false;
    gpio_intr_disable(static_cast<gpio_num_t>(USER_BUTTON_PIN));
    gpio_intr_disable(static_cast<gpio_num_t>(DEV_BUTTON_PIN));

    uint8_t event = 1;
    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(instance->_button_queue, &event, &task_woken);
    if (task_woken) portYIELD_FROM_ISR();
}

bool PowerManager::readButtonEvent(WakeupSource* source)
{
    if (!_button_queue || !source) return false;
    uint8_t event = 0;
    if (xQueueReceive(_button_queue, &event, 0) != pdTRUE) return false;

    // Allow contact bounce and a nearly simultaneous second contact to settle.
    // IRQs stay disabled until clearButtonEvents() rearms them after release.
    vTaskDelay(pdMS_TO_TICKS(30));
    const bool user = digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED;
    const bool dev = digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED;
    if (user && dev) {
        *source = WAKEUP_BOTH_BUTTONS;
    } else if (dev) {
        *source = WAKEUP_DEV_BUTTON;
    } else if (user) {
        *source = WAKEUP_USER_BUTTON;
    } else {
        // A very short bounce can disappear before classification. Do not
        // manufacture a USER event; safely rearm and let the caller wait again.
        clearButtonEvents();
        return false;
    }
    return true;
}

void PowerManager::clearButtonEvents()
{
    disarmButtonWakeup();
    waitForButtonsReleased();
    if (_button_queue) {
        uint8_t discarded = 0;
        while (xQueueReceive(_button_queue, &discarded, 0) == pdTRUE) {}
    }
    armButtonWakeup();
}

void PowerManager::armButtonWakeup()
{
    if (!_button_queue) return;
    _button_irq_armed = true;
    gpio_intr_enable(static_cast<gpio_num_t>(USER_BUTTON_PIN));
    gpio_intr_enable(static_cast<gpio_num_t>(DEV_BUTTON_PIN));
}

void PowerManager::disarmButtonWakeup()
{
    _button_irq_armed = false;
    gpio_intr_disable(static_cast<gpio_num_t>(USER_BUTTON_PIN));
    gpio_intr_disable(static_cast<gpio_num_t>(DEV_BUTTON_PIN));
}

void PowerManager::waitForButtonsReleased()
{
    while (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
           digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Require a stable release before enabling the HIGH-level wake interrupt.
    vTaskDelay(pdMS_TO_TICKS(30));
}

bool PowerManager::enableAutoLightSleep()
{
    esp_pm_config_t config{};
    config.max_freq_mhz = 160;
    config.min_freq_mhz = 40;
    config.light_sleep_enable = true;
    const esp_err_t err = esp_pm_configure(&config);
    if (err != ESP_OK) {
        ESP_LOGE(POWER_TAG, "Auto Light-sleep unavailable, err=%d", err);
        return false;
    }

    if (!_no_light_sleep_lock) {
        const esp_err_t lock_err = esp_pm_lock_create(
            ESP_PM_NO_LIGHT_SLEEP, 0, "outside_work", &_no_light_sleep_lock);
        if (lock_err != ESP_OK) {
            ESP_LOGE(POWER_TAG, "PM lock creation failed, err=%d", lock_err);
            return false;
        }
    }
    ESP_LOGI(POWER_TAG, "Auto Light-sleep enabled");
    return true;
}

void PowerManager::setWorkActive(bool active)
{
    if (!_no_light_sleep_lock || active == _work_active) return;
    if (active) esp_pm_lock_acquire(_no_light_sleep_lock);
    else esp_pm_lock_release(_no_light_sleep_lock);
    _work_active = active;
}
