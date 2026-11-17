// Define the classes of LED and buzzer
#include <Arduino.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "sensor.h"
#include "pins.h"
#include "config.h"
#include "driver/rtc_io.h"

// Battery power sampling initialization
void PowerManager::power_init()
{
    // Configure battery power sampling pin
    pinMode(BATTERY_PIN, INPUT);
}

// Read battery power value
uint8_t PowerManager::get_battery_value()
{
    uint32_t Vbatt = 0;

    // Sampling 32 times, taking the average
    for(int i = 0; i < 32; i++)
    {
        Vbatt += analogReadMilliVolts(BATTERY_PIN);
    }
    // 1:2 voltage divider, calculate the actual voltage value
    float Vbattf = (Vbatt * 2) / 32.0 / 1000.0;
    if (Vbattf < BATTERY_LOW_THRESHOLD)
    {
        Vbattf = BATTERY_LOW_THRESHOLD;
    }
    if(Vbattf > BATTERY_HIGH_THRESHOLD)
    {
        Vbattf = BATTERY_HIGH_THRESHOLD;
    }
    // Output voltage value
    ESP_LOGI(POWER_TAG, "battery voltage: %.3f", Vbattf);
    // Calculate battery percentage
    uint8_t bat_perc = (Vbattf - BATTERY_LOW_THRESHOLD) * 100.0f / (BATTERY_HIGH_THRESHOLD - BATTERY_LOW_THRESHOLD) + 0.5f;
    ESP_LOGI(POWER_TAG, "battery percentage: %d%%", bat_perc);
    return bat_perc;
}

// Detect wake-up source: ESP-IDF API + read button level
void PowerManager::detectWakeupSource()
{
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

    // Wait for a short period of time for the GPIO level to stabilize before reading again
    vTaskDelay(pdMS_TO_TICKS(20));
    bool user_pressed = (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED);
    bool dev_pressed  = (digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED);

    switch (reason)
    {
    // C3’s GPIO wake-up cannot distinguish which pin, and it depends on reading the level.
    case ESP_SLEEP_WAKEUP_GPIO:
        if (user_pressed && dev_pressed) {
            _wakeup_src = WAKEUP_BOTH_BUTTONS;
            ESP_LOGI(POWER_TAG, "Wakeup: BOTH buttons pressed");
        } else if (user_pressed) {
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: USER button");
        } else if (dev_pressed) {
            _wakeup_src = WAKEUP_DEV_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: DEV button");
        } else {
            // The button has been released and cannot be determined which one it is. The default is USER.
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGW(POWER_TAG, "Wakeup: GPIO but no button held, default USER");
        }
        break;

    default:
        _wakeup_src = WAKEUP_POWER_ON;
        ESP_LOGI(POWER_TAG, "Wakeup: power on / reset (reason=%d)", reason);
        break;
    }
}

// Waiting for wake-up button level reset
void PowerManager::wait_wakeup_button_intend()
{

    while(gpio_get_level(USER_BUTTON_PIN) == BUTTON_PRESSED || gpio_get_level(DEV_BUTTON_PIN) == BUTTON_PRESSED)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

}

// Enter sleep mode
void PowerManager::deep_sleep()
{
    // Waiting for wake-up pin level reset
    wait_wakeup_button_intend();
    // Configure key wake-up pin
// Wake-up pin configuration of in-vehicle module
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(USER_BUTTON_PIN) | BIT(DEV_BUTTON_PIN),
        BUTTON_PRESSED ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);

    // Go to sleep
    ESP_LOGI(POWER_TAG, "Going to sleep now");
    esp_deep_sleep_start();
    // It will not be executed till here.
}

// Key wake-up pin initialized to input mode
void PowerManager::wakeup_gpio_init()
{
// Configure key wake-up pin

    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}







