#ifndef RGB_LED_CONTROLLER_H
#define RGB_LED_CONTROLLER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"

// ============================================================
// RGB LED 中间层接口设计稿
// ============================================================
//
// 设计边界：
// - Adafruit_NeoPixel 已经负责 WS2812 / NeoPixel 底层驱动。
// - RgbLedController 只负责给主程序提供简单灯效 API。
// - 主程序只调用 solid / blink / breathe / off 等接口。
// - RGB task 是唯一真正操作 strip 和调用 strip.show() 的地方。
//
// strip.setPixelColor() / strip.fill() 只是修改库内部缓存。
// strip.show() 才会把缓存里的颜色数据发送到灯珠上，让灯真正刷新。

// ======================== 配置宏 ========================

#define RGB_LED_COUNT                  4

#define RGB_LED_DEFAULT_BRIGHTNESS     64

#define RGB_LED_MAX_BRIGHTNESS         96

#define RGB_LED_BRIGHTNESS_LOW_VALUE   24

#define RGB_LED_BRIGHTNESS_MED_VALUE   48

#define RGB_LED_BRIGHTNESS_HIGH_VALUE  80

#define RGB_LED_FRAME_INTERVAL_MS      20

#define RGB_LED_TASK_STACK_WORDS       4096

#define RGB_LED_TASK_PRIORITY          1// 低优先级，低于通信/主控制任务，不要高于 WiFi/ESP-NOW 相关任务

#define RGB_LED_COMMAND_QUEUE_LENGTH   8
// 常见 WS2812 配置：GRB 颜色顺序，800 kHz 通信频率。
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
#define RGB_COLOR_ORANGE      RgbColor{255, 64, 0}
#define RGB_COLOR_YELLOW      RgbColor{255, 240, 0}
#define RGB_COLOR_GREEN       RgbColor{0, 255, 0}
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

    // 使用亮度档位，具体数值由 RGB_LED_BRIGHTNESS_*_VALUE 宏决定。
    RgbBrightnessLevel brightness = RgbBrightnessLevel::Default;

    // Blink：完整闪烁周期，默认 50% 占空比。
    // Breathe：一次完整变亮再变暗的周期。
    uint16_t period_ms = 1000;
    RgbSensorSide side = RgbSensorSide::Left;
    bool value = false;
    uint16_t distance_cm = UINT16_MAX;
};

class RgbLedController {
public:
    RgbLedController();
    ~RgbLedController();

    // 初始化 NeoPixel、创建命令队列、创建 RGB task。
    bool begin();

    // 停止 RGB task、熄灭灯珠、删除命令队列。
    void end();

    // 设置默认亮度档位，具体数值由宏决定。
    bool setBrightness(RgbBrightnessLevel brightness);
    RgbBrightnessLevel brightness() const;

    // 主程序使用的灯效 API。
    // 这些接口只负责投递命令，不直接操作 strip。
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
    // Adafruit_NeoPixel 是真正的底层驱动。
    // begin() 后只允许 RGB task 操作这个对象。
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

    // 私有 task 框架。
    // begin() 内部创建 task，end() 内部删除 task。
    static void taskEntry(void* arg);
    void taskLoop();
    void setTaskRunning(bool running);
    bool taskShouldRun() const;
    TaskHandle_t taskHandle() const;
    void setTaskHandle(TaskHandle_t handle);
    void setCurrentMode(RgbLedMode mode);
    void setEffectFinished(bool finished);
    RgbBrightnessLevel currentBrightness() const;

    // 统一投递命令，内部使用 xQueueOverwrite() 覆盖旧命令。
    bool sendCommand(const RgbLedCommand& command);
};

// ============================================================
// RGB task 实现建议
// ============================================================
//
// taskLoop() 推荐结构：
//
// while (_task_running) {
//     RgbLedCommand new_cmd;
//     if (xQueueReceive(_command_queue, &new_cmd, 0) == pdTRUE) {
//         _current_command = new_cmd;
//         _current_mode = new_cmd.mode;
//         _effect_start_ms = millis();
//     }
//
//     // 根据 _current_command.mode 渲染 Off / Solid / Blink / Breathe。
//     // 渲染函数由 .cpp 自己组织，不需要在头文件提前声明。
//     // 所有 strip.fill()、strip.setPixelColor()、strip.show()
//     // 都放在这个 task 里。
//
//     vTaskDelay(pdMS_TO_TICKS(RGB_LED_FRAME_INTERVAL_MS));
// }

// ============================================================
// 主程序调用示例
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
