#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <esp_pm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "pins.h"

enum LED_PERIOD {
    LED_PERIOD_1 = 2000,
    LED_PERIOD_2 = 1000,
    LED_PERIOD_3 = 500,
};

enum LED_SPEED {
    LED_SPEED_1 = 10,
    LED_SPEED_2 = 15,
    LED_SPEED_3 = 20,
};

enum WakeupSource {
    WAKEUP_POWER_ON,
    WAKEUP_USER_BUTTON,
    WAKEUP_DEV_BUTTON,
    WAKEUP_BOTH_BUTTONS,
};

class LEDControler {
public:
    void led_init();
    void led_on();
    void led_off();
    void blink(LED_PERIOD period);
    void breath(LED_SPEED speed);
};

class PowerManager {
public:
    void power_init();
    uint8_t get_battery_value();

    // Existing two buttons are also wake sources while Auto Light-sleep is active.
    bool wakeup_gpio_init();
    QueueHandle_t buttonQueue() const { return _button_queue; }
    bool readButtonEvent(WakeupSource* source);
    void clearButtonEvents();

    bool enableAutoLightSleep();
    void setWorkActive(bool active);

private:
    static void ARDUINO_ISR_ATTR buttonIsr();
    static PowerManager* _instance;

    void armButtonWakeup();
    void disarmButtonWakeup();
    void waitForButtonsReleased();

    QueueHandle_t _button_queue = nullptr;
    esp_pm_lock_handle_t _no_light_sleep_lock = nullptr;
    bool _work_active = false;
    volatile bool _button_irq_armed = false;
};

#endif
