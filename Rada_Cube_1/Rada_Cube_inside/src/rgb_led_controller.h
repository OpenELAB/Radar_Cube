#ifndef RGB_LED_CONTROLLER_H
#define RGB_LED_CONTROLLER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pins.h"

// RgbLedController is only responsible for physical lighting effect rendering and does not understand upper-layer business events.
#define RGB_LED_COUNT                    4
#define RGB_LED_DEFAULT_BRIGHTNESS       64
#define RGB_LED_MAX_BRIGHTNESS           96
#define RGB_LED_FRAME_INTERVAL_MS        20
#define RGB_LED_TASK_STACK_WORDS         4096
#define RGB_LED_TASK_PRIORITY            1
#define RGB_LED_COMMAND_QUEUE_LENGTH     1
#define RGB_LED_PIXEL_TYPE               (NEO_GRB + NEO_KHZ800)

// The physical index follows the current wiring sequence, and the final direction needs to be confirmed through board testing.
#define RGB_LED_INDEX_RIGHT              0
#define RGB_LED_INDEX_TOP                1
#define RGB_LED_INDEX_LEFT               2
#define RGB_LED_INDEX_BOTTOM             3

#define RGB_LED_BLINK_SLOW_PERIOD_MS     1000
#define RGB_LED_BLINK_MEDIUM_PERIOD_MS   500
#define RGB_LED_BLINK_FAST_PERIOD_MS     200

#define RGB_LED_BREATHE_SLOW_PERIOD_MS   2400
#define RGB_LED_BREATHE_MEDIUM_PERIOD_MS 1600
#define RGB_LED_BREATHE_FAST_PERIOD_MS   900

struct RgbColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

#define RGB_COLOR_BLACK       RgbColor{0, 0, 0}
#define RGB_COLOR_RED         RgbColor{255, 0, 0}
#define RGB_COLOR_ORANGE      RgbColor{255, 64, 0}
#define RGB_COLOR_YELLOW      RgbColor{255, 240, 0}
#define RGB_COLOR_GREEN       RgbColor{0, 255, 0}
#define RGB_COLOR_BLUE        RgbColor{0, 64, 255}
#define RGB_COLOR_WHITE       RgbColor{255, 255, 255}
#define RGB_COLOR_SOFT_AMBER  RgbColor{120, 42, 0}

/*
 * The fixed light area only describes the physical pixels that need to be lit:
 * - ONLY_LEFT/ONLY_RIGHT: Single side light.
 * - ONLY_SIDES: The left and right side lights, the upper and lower lights are not involved.
 * - LEFT/RIGHT: Corresponds to the working area composed of side lights and upper and lower lights.
 */
enum LedZone : uint8_t {
    LED_ZONE_NONE = 0,
    LED_ZONE_ALL,          // four lights
    LED_ZONE_ONLY_LEFT,    // Left light only
    LED_ZONE_ONLY_RIGHT,   // Right light only
    LED_ZONE_ONLY_SIDES,   // Left and right side lights
    LED_ZONE_LEFT,         // Left light + upper light + lower light
    LED_ZONE_RIGHT         // Right light + upper light + lower light
};

// The upper layer only selects the speed gear, and the specific flashing and breathing cycles are defined by this module.
enum RgbEffectSpeed : uint8_t {
    RGB_EFFECT_SPEED_SLOW = 0,
    RGB_EFFECT_SPEED_MEDIUM,
    RGB_EFFECT_SPEED_FAST
};

// Limited-duration physical animation primitive, the mapping of business events to animation belongs to FeedbackController.
enum RgbAnimation : uint8_t {
    RGB_ANIMATION_NONE = 0,
    RGB_ANIMATION_CHASE_CLOCKWISE,
    RGB_ANIMATION_CHASE_COUNTERCLOCKWISE,
    RGB_ANIMATION_FLASH,
    RGB_ANIMATION_FADE_OUT
};

/*
 * RGB hardware controller.
 *
 * The only RGB task is exclusive to NeoPixel; the lighting effect API only delivers the latest commands and does not write pixels directly.
 * The command queue length is 1, the new visual state overwrites the old state, and no queue playback is performed.
 * The command API that returns bool only indicates whether the command was successfully submitted, but does not indicate that the lighting effect has been completed.
 */
class RgbLedController {
public:
    RgbLedController();
    ~RgbLedController();

    // Initialize power supply, queue, synchronization object and rendering task; turn off the lights by default after success.
    bool begin();

    // Collaborate to stop the task, clear the lights, turn off the power supply and release resources; repeated calls will not produce additional effects.
    void end();

    /*
     * Set the brightness used for subsequent and current lighting effects, and the input is clamped to RGB_LED_MAX_BRIGHTNESS.
     * If the visual command has not been executed, only the brightness carried by the command will be updated, and its effect type will not be overwritten.
     */
    bool setBrightness(uint8_t brightness);

    // Returns the latest successfully submitted brightness request value that has been clamped.
    uint8_t brightness() const;

    // Submit the continuous lights-off state.
    bool off();

    // Submit remains on.
    bool solid(const RgbColor& color, LedZone zone = LED_ZONE_ALL);

    // Submit a continuous flashing state, the period is determined by the RgbEffectSpeed ​​mapping.
    bool blink(const RgbColor& color,
               RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM,
               LedZone zone = LED_ZONE_ALL);

    // Submit the continuous breathing state, the period is determined by the RgbEffectSpeed ​​mapping.
    bool breathe(const RgbColor& color,
                 RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM,
                 LedZone zone = LED_ZONE_ALL);

    /*
     * Play limited animation:
     * - CHASE_CLOCKWISE: Green, BOTTOM -> LEFT -> TOP -> RIGHT, about 1 second.
     * - CHASE_COUNTERCLOCKWISE: green, reverse order, about 1 second.
     * - FLASH: The four lights flash green 3 times quickly, with a period of about 180 ms.
     * - FADE_OUT: Soft amber press RIGHT -> TOP -> LEFT -> BOTTOM to fade, about 1 second.
     * - NONE: Equivalent to off().
     *
     * New visual commands immediately preempt animation; brightness commands do not reset the beat. Turns off when the animation is complete,
     * Do not restore old effects and do not retain the last frame.
     */
    bool playAnimation(RgbAnimation animation);

    // Returns true when the limited animation is waiting to be executed or running; continuous lighting effects are not considered busy.
    bool isBusy() const;

    // Return true when begin() succeeds and has not yet entered end().
    bool isBegun() const;

    // Return true when the rendering task has been created and has not exited.
    bool isTaskRunning() const;

private:
    enum CommandType : uint8_t {
        COMMAND_OFF = 0,
        COMMAND_SOLID,
        COMMAND_BLINK,
        COMMAND_BREATHE,
        COMMAND_ANIMATION,
        COMMAND_SET_BRIGHTNESS
    };

    struct Command {
        CommandType type = COMMAND_OFF;
        RgbColor color = RGB_COLOR_BLACK;
        LedZone zone = LED_ZONE_ALL;
        RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM;
        uint8_t brightness = RGB_LED_DEFAULT_BRIGHTNESS;
        RgbAnimation animation = RGB_ANIMATION_NONE;
    };

    // _strip only allows RGB task access; each begin()/end() cycle is created and released independently.
    Adafruit_NeoPixel* _strip = nullptr;
    QueueHandle_t _command_queue = nullptr;

    // The lock sequence is fixed to api -> queue; RGB task only acquires queue to avoid forced exit of legacy api lock.
    SemaphoreHandle_t _api_mutex = nullptr;
    SemaphoreHandle_t _queue_mutex = nullptr;
    SemaphoreHandle_t _task_stopped = nullptr;
    TaskHandle_t _task_handle = nullptr;
    mutable portMUX_TYPE _state_mux = portMUX_INITIALIZER_UNLOCKED;

    Command _active_command;
    uint8_t _requested_brightness = RGB_LED_DEFAULT_BRIGHTNESS;
    uint8_t _current_brightness = RGB_LED_DEFAULT_BRIGHTNESS;
    uint32_t _effect_start_ms = 0;
    bool _animation_busy = false;
    bool _pending_animation = false;
    bool _begun = false;
    bool _task_running = false;
    bool _stopping = false;

    static void taskEntry(void* arg);

    // Read the latest command and render according to a fixed frame cycle; show() is only called once per regular frame.
    void taskLoop();

    // Receives commands by value to inject the latest brightness or merge pending brightness updates.
    bool sendCommand(Command command);

    // Called only by RGB task, switches the current effect and maintains animation busy state.
    void handleCommand(const Command& command);

    // Calculate four pixels based on the current effect; do not call NeoPixel API directly.
    void renderFrame(uint32_t now, RgbColor pixels[RGB_LED_COUNT]);

    void setAnimationBusy(bool busy);
    void setTaskHandle(TaskHandle_t handle);
    void setTaskRunning(bool running);
    bool taskShouldRun() const;

    // Turn off the lamp beads and release the RMT resources occupied by NeoPixel on ESP32.
    void releaseStripHardware();
};

#endif
