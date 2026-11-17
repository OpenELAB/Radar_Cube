#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "config.h"
#include "pins.h"


// Battery status
enum BATTERY_STAT
{
    NORMAL,
    LOW_ENGERGY,
    NO_ENERGY
};

// Wake-up source (unified enumeration, no distinction between INSIDE/OUTSIDE)
enum WakeupSource {
    WAKEUP_POWER_ON,        // First power on/reset
    WAKEUP_USER_BUTTON,     // Wake up by user key press
    WAKEUP_DEV_BUTTON,      // Develop button wake-up
    WAKEUP_BOTH_BUTTONS,    // Two buttons are pressed at the same time (depending on the hardware wiring, this may occur)
    WAKEUP_LORA,            // Lora wake up (OUTSIDE only)
};


/**
 * Battery Management & Sleep Control
 *
 * Usage example:
 *   PowerManager Power;
 *   Power.wakeup_gpio_init();      // Initialize wake-up pin
 *   Power.get_wakeup_reason();     // Get wake-up reason
 *   Power.power_init();            // Initialize battery collection
 *   uint8_t bat = Power.get_battery_value();  // 0-100
 *   Power.deep_sleep();            // Enter deep sleep
 */
class PowerManager
{
public:
    // Initialize battery voltage acquisition pin
    void power_init();
    // Wait for the wake-up pin level to reset and enter sleep mode
    void deep_sleep();
    // Wake-up pin initialization
    void wakeup_gpio_init();
    // Waiting for wake-up pin level reset
    void wait_wakeup_button_intend();
    // Detect the cause of wake-up (ESP-IDF API + read button level), and store the result in _wakeup_src
    void detectWakeupSource();
    // Get the detected wake-up source
    WakeupSource getWakeupSource() const { return _wakeup_src; }
    // Get the battery voltage value
    uint8_t get_battery_value();

private:
    WakeupSource _wakeup_src = WAKEUP_POWER_ON;
};

#endif
