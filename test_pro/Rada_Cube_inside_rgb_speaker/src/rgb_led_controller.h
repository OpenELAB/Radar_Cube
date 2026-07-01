#ifndef RGB_LED_CONTROLLER_H
#define RGB_LED_CONTROLLER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"

#define RGB_LED_COUNT                  4
#define RGB_LED_DEFAULT_BRIGHTNESS     64
#define RGB_LED_MAX_BRIGHTNESS         96
#define RGB_LED_BRIGHTNESS_LOW_VALUE   24
#define RGB_LED_BRIGHTNESS_MED_VALUE   48
#define RGB_LED_BRIGHTNESS_HIGH_VALUE  80
#define RGB_LED_FRAME_INTERVAL_MS      20
#define RGB_LED_TASK_STACK_WORDS       4096
#define RGB_LED_TASK_PRIORITY          1
#define RGB_LED_COMMAND_QUEUE_LENGTH   8
#define RGB_LED_PIXEL_TYPE             (NEO_GRB + NEO_KHZ800)

constexpr uint8_t RGB_LED_INDEX_RIGHT = 0;
constexpr uint8_t RGB_LED_INDEX_TOP = 1;
constexpr uint8_t RGB_LED_INDEX_LEFT = 2;
constexpr uint8_t RGB_LED_INDEX_BOTTOM = 3;

struct RgbColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

#define RGB_COLOR_BLACK       RgbColor{0, 0, 0}
#define RGB_COLOR_RED         RgbColor{255, 0, 0}
#define RGB_COLOR_ORANGE      RgbColor{255, 96, 0}
#define RGB_COLOR_YELLOW      RgbColor{255, 180, 0}
#define RGB_COLOR_GREEN       RgbColor{0, 255, 32}
#define RGB_COLOR_BLUE        RgbColor{0, 64, 255}
#define RGB_COLOR_WHITE       RgbColor{255, 255, 255}
#define RGB_COLOR_SOFT_AMBER  RgbColor{120, 42, 0}
#define RGB_COLOR_DARK_AMBER  RgbColor{48, 18, 0}

enum class RgbLedMode : uint8_t {
    Off,
    Solid,
    Blink,
    Breathe,
    Startup,
    Standby,
    UnpairedWarning,
    Pairing,
    PairSuccess,
    FactoryReset,
    SystemStatus,
    ParkingDistance
};

enum class RgbBrightnessLevel : uint8_t {
    Default,
    Low,
    Medium,
    High
};

enum class RgbSensorSide : uint8_t {
    Left,
    Right
};

enum class RgbLedAction : uint8_t {
    SetMode,
    SetConnected,
    ConnectedPulse,
    SetLost,
    SetLowBattery,
    SetInsideLowBattery,
    SetFault,
    UpdateParkingDistance,
    ClearParkingDistance
};

struct RgbLedCommand {
    RgbLedAction action = RgbLedAction::SetMode;
    RgbLedMode mode = RgbLedMode::Off;
    RgbColor color = RGB_COLOR_BLACK;
    RgbBrightnessLevel brightness = RgbBrightnessLevel::Default;
    uint16_t period_ms = 1000;
    RgbSensorSide side = RgbSensorSide::Left;
    bool value = false;
    uint16_t distance_cm = UINT16_MAX;
};

class RgbLedController {
public:
    RgbLedController();
    ~RgbLedController();

    bool begin();
    void end();

    bool setBrightness(RgbBrightnessLevel brightness);
    RgbBrightnessLevel brightness() const;

    bool off();
    bool solid(const RgbColor& color);
    bool blink(const RgbColor& color, uint16_t period_ms);
    bool breathe(const RgbColor& color, uint16_t period_ms);

    bool startup();
    bool standby();
    bool unpairedWarning();
    bool pairing();
    bool pairSuccess();
    bool factoryReset();
    bool showSystemStatus();
    bool setSensorConnected(RgbSensorSide side, bool connected);
    bool sensorConnectedPulse(RgbSensorSide side);
    bool setSensorLost(RgbSensorSide side, bool lost);
    bool setSensorLowBattery(RgbSensorSide side, bool low);
    bool setInsideLowBattery(bool low);
    bool setSensorFault(RgbSensorSide side, bool fault);
    bool updateParkingDistance(uint16_t distance_cm);
    bool clearParkingDistance();
    bool effectFinished() const;

    bool isBegun() const;
    bool isTaskRunning() const;
    RgbLedMode currentMode() const;

private:
    struct RuntimeStatus {
        bool left_connected = false;
        bool right_connected = false;
        bool left_lost = false;
        bool right_lost = false;
        bool left_low_battery = false;
        bool right_low_battery = false;
        bool inside_low_battery = false;
        bool left_fault = false;
        bool right_fault = false;
        bool parking_active = false;
        uint16_t parking_distance_cm = UINT16_MAX;
        uint32_t left_connected_pulse_start_ms = 0;
        uint32_t right_connected_pulse_start_ms = 0;
        uint32_t left_lost_start_ms = 0;
        uint32_t right_lost_start_ms = 0;
        uint32_t left_fault_flash_start_ms = 0;
        uint32_t right_fault_flash_start_ms = 0;
    };

    Adafruit_NeoPixel _strip;
    QueueHandle_t _command_queue = nullptr;
    TaskHandle_t _task_handle = nullptr;
    mutable portMUX_TYPE _state_mux = portMUX_INITIALIZER_UNLOCKED;

    RgbLedCommand _current_command;
    RgbLedMode _current_mode = RgbLedMode::Off;
    RgbBrightnessLevel _current_brightness = RgbBrightnessLevel::Default;
    RuntimeStatus _runtime_status;
    uint32_t _effect_start_ms = 0;
    uint32_t _last_render_ms = 0;
    bool _effect_finished = true;
    bool _begun = false;
    bool _task_running = false;

    static void taskEntry(void* arg);
    void taskLoop();
    void setTaskRunning(bool running);
    bool taskShouldRun() const;
    TaskHandle_t taskHandle() const;
    void setTaskHandle(TaskHandle_t handle);
    void setCurrentMode(RgbLedMode mode);
    void setEffectFinished(bool finished);
    RgbBrightnessLevel currentBrightness() const;
    bool sendCommand(const RgbLedCommand& command);
};

#endif
