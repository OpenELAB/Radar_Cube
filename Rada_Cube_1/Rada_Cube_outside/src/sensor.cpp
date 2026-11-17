// Define the classes of LED and buzzer
#include <Arduino.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "sensor.h"
#include "pins.h"
#include "config.h"
#include "driver/rtc_io.h"

// Initialize LED pin
void LEDControler::led_init()
{
    pinMode(LED_PIN, OUTPUT);
}

void LEDControler::led_on()
{
    digitalWrite(LED_PIN, LED_ACTIVE_LEVEL);
}
void LEDControler::led_off()
{
    digitalWrite(LED_PIN, LED_INACTIVE_LEVEL);
}


// period incoming delay time, unit milliseconds
void LEDControler::blink(LED_PERIOD period)
{
    // Unbind the LEDC channel and reset it to output mode
    pinMode(LED_PIN, OUTPUT);

    const uint32_t led_period = period / 2;
    digitalWrite(LED_PIN, LED_ACTIVE_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(led_period));
    digitalWrite(LED_PIN, LED_INACTIVE_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(led_period));
}

// Incoming delay speed, unit ms, the smaller the value, the faster the speed
void LEDControler::breath(LED_SPEED speed)
{
    ledcAttach(LED_PIN, LED_FREQ, LEDC_TIMER_8_BIT);
    for (int duty = 0; duty < 255; duty += 2)
    {
        ledcWrite(LED_PIN, duty);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
    for (int duty = 255; duty >= 0; duty -= 2)
    {
        ledcWrite(LED_PIN, duty);
        vTaskDelay(pdMS_TO_TICKS(speed));
    }
    ledcDetach(LED_PIN);
}


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


// #ifdef OUTSIDE
    // C6’s EXT1 wake-up can determine the specific pin through bitmask
    case ESP_SLEEP_WAKEUP_EXT1:
    {
        uint64_t mask = esp_sleep_get_ext1_wakeup_status();
        bool user_ext = (mask & (1ULL << USER_BUTTON_PIN));
        bool dev_ext  = (mask & (1ULL << DEV_BUTTON_PIN));

        if (user_ext && dev_ext) {
            _wakeup_src = WAKEUP_BOTH_BUTTONS;
            ESP_LOGI(POWER_TAG, "Wakeup: BOTH buttons (EXT1)");
        } else if (user_ext) {
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: USER button (EXT1)");
        } else if (dev_ext) {
            _wakeup_src = WAKEUP_DEV_BUTTON;
            ESP_LOGI(POWER_TAG, "Wakeup: DEV button (EXT1)");
        } else {
            _wakeup_src = WAKEUP_USER_BUTTON;
            ESP_LOGW(POWER_TAG, "Wakeup: EXT1 but unknown pin, default USER");
        }
        break;
    }

    // C6’s GPIO wakeup = Lora wakeup
    case ESP_SLEEP_WAKEUP_GPIO:
        _wakeup_src = WAKEUP_LORA;
        ESP_LOGI(POWER_TAG, "Wakeup: Lora");
        break;
// #endif

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
// #ifdef OUTSIDE
    //  Configure key wake-up source
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKEUP_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    // Configure Lora wake-up source
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(LORA_WAKE_PIN),
        LORA_WAKE_ACTIVE ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
// #endif
    // Go to sleep
    ESP_LOGI(POWER_TAG, "Going to sleep now");
    esp_deep_sleep_start();
    // It will not be executed till here.
}

// Key wake-up pin initialized to input mode
void PowerManager::wakeup_gpio_init()
{
// Configure key wake-up pin

// #ifdef OUTSIDE
    pinMode(LORA_WAKE_PIN, INPUT);
// #endif
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}







