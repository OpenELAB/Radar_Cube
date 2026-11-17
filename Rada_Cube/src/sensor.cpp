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

#ifdef INSIDE
// Initialize buzzer period
const uint16_t BeeperControler::PERIODS[] = {
    0,           // mute
    1,           // long cry
    250,         // Buzzer period 250ms
    500,         // Buzzer period 500ms
    1000         // Buzzer period 1000ms
};

// Check the buzzer periodic table
uint16_t BeeperControler::get_period(uint8_t mode)
{
    if(mode > sizeof(PERIODS)/sizeof(PERIODS[0]))
    {
        return 0; // Silent by default
    }
    return PERIODS[mode];
}

// Initialize buzzer pin
void BeeperControler::beeper_init()
{
    pinMode(BEEPER_PIN, OUTPUT);
    // ESP32 Arduino v3.0+ recommends using ledcAttach to initialize the fixed frequency, but its parameter is not the third one as the duty cycle bit width
    // If you use ledcWriteTone, it will automatically configure the channel at the bottom
    ledcAttach(BEEPER_PIN, BEEPER_FREQ, 8); // Set to 8-bit resolution
}

// Buzzer control
void BeeperControler::beep()
{
    // If you use the original ledcWrite(BEEPER_PIN, BEEPER_DUTY);
    // The sound may be greatly reduced due to changes in the hardware duty cycle calculation method after experiencing ledcWriteTone.
    // To maximize loudness (50% duty cycle), 50% = 127 at 8-bit resolution.
    // If want louder sound we should give 127. In the original code, BEEPER_DUTY = 4, which is a very small duty cycle, which will cause the sound to be very soft.
    
    // Directly use the more stable ledcWriteTone to generate constant frequency and maximize loudness

    // Reconstruct the buzzer response and change it to non-blocking. Periodic beeping requires external timing.

    ledcWriteTone(BEEPER_PIN, BEEPER_FREQ);
}
void BeeperControler::beep_stop()
{
    ledcWriteTone(BEEPER_PIN, 0);
}

// Play a rising tone of success tone
void BeeperControler::play_success_tone()
{
    uint32_t tones[] = {1046, 1318, 1568};
    for(int i = 0; i < 3; i++) {
        ledcWriteTone(BEEPER_PIN, tones[i]);
        vTaskDelay(pdMS_TO_TICKS(100));
        ledcWriteTone(BEEPER_PIN, 0); // Stop
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Play a falling tone failure tone
void BeeperControler::play_fail_tone()
{
    uint32_t tones[] = {1568, 1318, 1046, 784};
    for(int i = 0; i < 4; i++) {
        ledcWriteTone(BEEPER_PIN, tones[i]);
        vTaskDelay(pdMS_TO_TICKS(150)); 
    }
    
    ledcWriteTone(BEEPER_PIN, 0); 
}
#endif

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
#ifdef INSIDE
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
#endif

#ifdef OUTSIDE
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
#endif

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
#ifdef INSIDE
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(USER_BUTTON_PIN) | BIT(DEV_BUTTON_PIN),
        BUTTON_PRESSED ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif

#ifdef OUTSIDE
    //  Configure key wake-up source
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKEUP_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    // Configure Lora wake-up source
    esp_deep_sleep_enable_gpio_wakeup(
        BIT(LORA_WAKE_PIN),
        LORA_WAKE_ACTIVE ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
    // Go to sleep
    ESP_LOGI(POWER_TAG, "Going to sleep now");
    esp_deep_sleep_start();
    // It will not be executed till here.
}

// Key wake-up pin initialized to input mode
void PowerManager::wakeup_gpio_init()
{
// Configure key wake-up pin

#ifdef OUTSIDE
    pinMode(LORA_WAKE_PIN, INPUT);
#endif
    pinMode(USER_BUTTON_PIN, INPUT);
    pinMode(DEV_BUTTON_PIN, INPUT);
}







