#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "config.h"
#include "pins.h"

// LED strobe cycle
// enum LED_PERIOD
// {
//     LED_PERIOD_1 = 2000, // Strobe period 2s
//     LED_PERIOD_2 = 1000, // Strobe period 1s
//     LED_PERIOD_3 = 500, // Strobe period 0.5s
// };

// // LED breathing light cycle
// enum LED_SPEED
// {
//     LED_SPEED_1 = 10, // breathing period 2.56s
//     LED_SPEED_2 = 15, // breathing period 3.84s
//     LED_SPEED_3 = 20, // breathing period 5.12s
// };

// Buzzer sound cycle

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
 * LED control class
 *
 * Usage example:
 *   LEDControler Led;
 *   Led.led_init();
 *   Led.blink(LED_PERIOD_1);   // 2s periodic flashing
 *   Led.breath(LED_SPEED_1);   // Breathing light effect
 *   Led.led_off();
 */
// class LEDControler
// {
// public:
//     void led_init();
//     void led_on();
//     void led_off();
//     void blink(LED_PERIOD period);
//     void breath(LED_SPEED speed);
// };

/**
 * Buzzer control class (only used by INSIDE module)
 *
 * Usage example:
 *   BeeperControler Beeper;
 *   Beeper.beeper_init();
 *   Beeper.beep(BEEPER_PERIOD_2);  // 0.5s periodic beep
 *   Beeper.beep_stop();            // Stop beeping
 */
// class BeeperControler
// {
// public:
//     void beeper_init();
//     void beep();
//     void beep_stop();
//     void play_success_tone(); // Add success tone method
//     void play_fail_tone(); // Add failure tone method

//     // Check the buzzer periodic table
//     uint16_t get_period(uint8_t mode);

    
// private:
//     static const uint16_t PERIODS[]; // Define period array
// };

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
