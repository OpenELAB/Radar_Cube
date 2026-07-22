#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

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
    WAKEUP_TIMER,
    WAKEUP_USER_BUTTON,
    WAKEUP_DEV_BUTTON,
    WAKEUP_BOTH_BUTTONS,
    WAKEUP_UNKNOWN,
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
    // Put all externally powered peripherals in a safe state as early as possible.
    void prepareSafePins();
    void power_init();
    uint8_t get_battery_value();

    WakeupSource getWakeupSource();
    WakeupSource classifyWakeButtons();
    void beginButtonMonitoring();
    bool buttonEventPending() const { return _runtime_button_status != 0; }
    void clearButtonEvents() { _runtime_button_status = 0; }
    void waitForButtonsReleased();
    [[noreturn]] void enterDeepSleep(uint32_t sleep_ms,
                                     bool enable_timer = true);

private:
    static void ARDUINO_ISR_ATTR userButtonIsr();
    static void ARDUINO_ISR_ATTR devButtonIsr();
    static PowerManager* _instance;

    uint64_t _gpio_wakeup_status = 0;
    volatile uint32_t _runtime_button_status = 0;
    volatile bool _button_activity_observed = false;
};

#endif
