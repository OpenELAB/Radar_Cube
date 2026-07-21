#ifndef RGB_LED_CONTROLLER_H
#define RGB_LED_CONTROLLER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ============================================================
// RGB LED middle layer interface design draft
// ============================================================
//
// Design boundaries:
// - Adafruit_NeoPixel is already responsible for the WS2812 / NeoPixel underlying driver.
// - RgbLedController is only responsible for providing simple lighting effect API to the main program.
// - The main program only calls interfaces such as solid / blink / breathe / off.
// - The RGB task is the only place where strips are actually manipulated and strip.show() is called.
//
// strip.setPixelColor() / strip.fill() just modify the library’s internal cache.
// strip.show() will send the color data in the cache to the lamp bead, so that the lamp can be truly refreshed.

// ======================== Configuration Macro ========================

#define RGB_LED_PIN                    4

#define RGB_LED_PWR_PIN                2

#define RGB_LED_COUNT                  4

#define RGB_LED_DEFAULT_BRIGHTNESS     64

#define RGB_LED_MAX_BRIGHTNESS         96

#define RGB_LED_BRIGHTNESS_LOW_VALUE   24

#define RGB_LED_BRIGHTNESS_MED_VALUE   48

#define RGB_LED_BRIGHTNESS_HIGH_VALUE  80

#define RGB_LED_FRAME_INTERVAL_MS      20

#define RGB_LED_TASK_STACK_WORDS       4096

#define RGB_LED_TASK_PRIORITY          1 // Low priority, lower than communication/main control tasks, not higher than WiFi/ESP-NOW related tasks

// RGB lights are status indicators and only need to retain the latest lighting effects.
// When multiple commands are sent continuously in a short period of time, old commands can be overwritten by new commands.
#define RGB_LED_COMMAND_QUEUE_LENGTH   1

// Common WS2812 configuration: GRB color sequence, 800 kHz communication frequency.
#define RGB_LED_PIXEL_TYPE             (NEO_GRB + NEO_KHZ800)

// ======================== Data structure ========================

struct RgbColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

#define RGB_COLOR_BLACK     RgbColor{0, 0, 0}
#define RGB_COLOR_RED       RgbColor{255, 0, 0}
#define RGB_COLOR_ORANGE    RgbColor{255, 96, 0}
#define RGB_COLOR_YELLOW    RgbColor{255, 180, 0}
#define RGB_COLOR_GREEN     RgbColor{0, 255, 32}
#define RGB_COLOR_BLUE      RgbColor{0, 64, 255}
#define RGB_COLOR_WHITE     RgbColor{255, 255, 255}

enum class RgbLedMode : uint8_t {
    Off,      // Off
    Solid,    // Always on
    Blink,    // Flashing
    Breathe   // Breathing light
};

enum class RgbBrightnessLevel : uint8_t {
    Default,  // Use current default brightness
    Low,      // low brightness
    Medium,   // medium brightness
    High      // High brightness
};

struct RgbLedCommand {
    RgbLedMode mode = RgbLedMode::Off;
    RgbColor color = RGB_COLOR_BLACK;

    // Use the brightness level, the specific value is determined by the RGB_LED_BRIGHTNESS_*_VALUE macro.
    RgbBrightnessLevel brightness = RgbBrightnessLevel::Default;

    // Blink: complete blink cycle, default 50% duty cycle.
    // Breathe: A complete cycle of brightening and then darkening.
    uint16_t period_ms = 1000;
};

// ======================== Control class ========================

class RgbLedController {
public:
    RgbLedController();
    ~RgbLedController();

    // Initialize NeoPixel, create command queue, and create RGB task.
    bool begin();

    // Stop the RGB task, turn off the lamp beads, and delete the command queue.
    void end();

    // Set the default brightness level, the specific value is determined by macro.
    bool setBrightness(RgbBrightnessLevel brightness);
    RgbBrightnessLevel brightness() const;

    // Lighting effect API used by the main program.
    // These interfaces are only responsible for delivering commands and do not directly operate strip.
    bool off();
    bool solid(const RgbColor& color);
    bool blink(const RgbColor& color, uint16_t period_ms);
    bool breathe(const RgbColor& color, uint16_t period_ms);

    bool isBegun() const;
    bool isTaskRunning() const;
    RgbLedMode currentMode() const;

private:
    // Adafruit_NeoPixel is the real low-level driver.
    // Only RGB task is allowed to operate this object after begin().
    Adafruit_NeoPixel _strip;

    QueueHandle_t _command_queue = nullptr;
    TaskHandle_t _task_handle = nullptr;
    mutable portMUX_TYPE _state_mux = portMUX_INITIALIZER_UNLOCKED;

    RgbLedCommand _current_command;
    RgbLedMode _current_mode = RgbLedMode::Off;
    RgbBrightnessLevel _current_brightness = RgbBrightnessLevel::Default;
    uint32_t _effect_start_ms = 0;
    uint32_t _last_render_ms = 0;
    bool _begun = false;
    bool _task_running = false;

    // Private task framework.
    // begin() creates a task internally, and end() deletes a task internally.
    static void taskEntry(void* arg);
    void taskLoop();
    void setTaskRunning(bool running);
    bool taskShouldRun() const;
    TaskHandle_t taskHandle() const;
    void setTaskHandle(TaskHandle_t handle);
    void setCurrentMode(RgbLedMode mode);
    RgbBrightnessLevel currentBrightness() const;

    // Unified delivery of commands, using xQueueOverwrite() internally to overwrite old commands.
    bool sendCommand(const RgbLedCommand& command);
};

// ============================================================
// RGB task implementation suggestions
// ============================================================
//
// taskLoop() recommended structure:
//
// while (_task_running) {
//     RgbLedCommand new_cmd;
//     if (xQueueReceive(_command_queue, &new_cmd, 0) == pdTRUE) {
//         _current_command = new_cmd;
//         _current_mode = new_cmd.mode;
//         _effect_start_ms = millis();
//     }
//
//     // Render Off / Solid / Blink / Breathe according to _current_command.mode.
//     // The rendering function is organized by .cpp itself and does not need to be declared in advance in the header file.
//     // All strip.fill(), strip.setPixelColor(), strip.show()
//     // Put them all in this task.
//
//     vTaskDelay(pdMS_TO_TICKS(RGB_LED_FRAME_INTERVAL_MS));
// }

// ============================================================
// Main program call example
// ============================================================
//
// #include "rgb_led_controller.h"
//
// static RgbLedController RgbLed;
//
// void setup()
// {
//     RgbLed.begin();
//     RgbLed.setBrightness(RgbBrightnessLevel::Medium);
//     RgbLed.solid(RGB_COLOR_GREEN);
// }
//
// void onPairingStart()
// {
//     RgbLed.breathe(RGB_COLOR_BLUE, 1800);
// }
//
// void onPairSuccess()
// {
//     RgbLed.blink(RGB_COLOR_GREEN, 250);
// }
//
// void onWakeFail()
// {
//     RgbLed.blink(RGB_COLOR_RED, 300);
// }
//
// void updateParkingDistance(uint16_t distance_cm)
// {
//     if (distance_cm > 220) {
//         RgbLed.solid(RGB_COLOR_GREEN);
//     } else if (distance_cm > 170) {
//         RgbLed.breathe(RGB_COLOR_YELLOW, 1200);
//     } else if (distance_cm > 120) {
//         RgbLed.blink(RGB_COLOR_YELLOW, 600);
//     } else if (distance_cm > 70) {
//         RgbLed.blink(RGB_COLOR_ORANGE, 300);
//     } else {
//         RgbLed.blink(RGB_COLOR_RED, 180);
//     }
// }
//
// void beforeDeepSleep()
// {
//     RgbLed.off();
//     RgbLed.end();
// }

#endif
