#ifndef RGB_LED_CONTROLLER_H
#define RGB_LED_CONTROLLER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pins.h"

// RgbLedController 只负责物理灯效渲染，不理解上层业务事件。
#define RGB_LED_COUNT                    4
#define RGB_LED_DEFAULT_BRIGHTNESS       64
#define RGB_LED_MAX_BRIGHTNESS           96
#define RGB_LED_FRAME_INTERVAL_MS        20
#define RGB_LED_TASK_STACK_WORDS         4096
#define RGB_LED_TASK_PRIORITY            1
#define RGB_LED_COMMAND_QUEUE_LENGTH     1
#define RGB_LED_PIXEL_TYPE               (NEO_GRB + NEO_KHZ800)

// 物理索引沿用当前接线顺序，最终方向需要通过上板测试确认。
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
 * 固定灯区只描述需要点亮的物理像素：
 * - ONLY_LEFT/ONLY_RIGHT：单颗侧灯。
 * - ONLY_SIDES：左右两颗侧灯，上下灯不参与。
 * - LEFT/RIGHT：对应侧灯与上下灯组成的工作区域。
 */
enum LedZone : uint8_t {
    LED_ZONE_NONE = 0,
    LED_ZONE_ALL,          // 四灯
    LED_ZONE_ONLY_LEFT,    // 仅左侧灯
    LED_ZONE_ONLY_RIGHT,   // 仅右侧灯
    LED_ZONE_ONLY_SIDES,   // 左右侧灯
    LED_ZONE_LEFT,         // 左侧灯 + 上灯 + 下灯
    LED_ZONE_RIGHT         // 右侧灯 + 上灯 + 下灯
};

// 上层只选择速度档位，具体闪烁和呼吸周期由本模块定义。
enum RgbEffectSpeed : uint8_t {
    RGB_EFFECT_SPEED_SLOW = 0,
    RGB_EFFECT_SPEED_MEDIUM,
    RGB_EFFECT_SPEED_FAST
};

// 有限时长的物理动画原语，业务事件到动画的映射属于 FeedbackController。
enum RgbAnimation : uint8_t {
    RGB_ANIMATION_NONE = 0,
    RGB_ANIMATION_CHASE_CLOCKWISE,
    RGB_ANIMATION_CHASE_COUNTERCLOCKWISE,
    RGB_ANIMATION_FLASH,
    RGB_ANIMATION_FADE_OUT
};

/*
 * RGB 硬件控制器。
 *
 * 唯一的 RGB task 独占 NeoPixel；灯效 API 只投递最新命令，不直接写像素。
 * 命令队列长度为 1，新视觉状态覆盖旧状态，不做排队回放。
 * 返回 bool 的命令 API 仅表示命令是否成功提交，不表示灯效已经执行完成。
 */
class RgbLedController {
public:
    RgbLedController();
    ~RgbLedController();

    // 初始化供电、队列、同步对象和渲染 task；成功后默认关灯。
    bool begin();

    // 协作停止 task，清灯、关闭供电并释放资源；重复调用不产生额外效果。
    void end();

    /*
     * 设置后续和当前灯效使用的亮度，输入钳制到 RGB_LED_MAX_BRIGHTNESS。
     * 若视觉命令尚未执行，只更新该命令携带的亮度，不覆盖其效果类型。
     */
    bool setBrightness(uint8_t brightness);

    // 返回最近一次成功提交且已经钳制的亮度请求值。
    uint8_t brightness() const;

    // 提交持续关灯状态。
    bool off();

    // 提交持续常亮状态。
    bool solid(const RgbColor& color, LedZone zone = LED_ZONE_ALL);

    // 提交持续闪烁状态，周期由 RgbEffectSpeed 映射决定。
    bool blink(const RgbColor& color,
               RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM,
               LedZone zone = LED_ZONE_ALL);

    // 提交持续呼吸状态，周期由 RgbEffectSpeed 映射决定。
    bool breathe(const RgbColor& color,
                 RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM,
                 LedZone zone = LED_ZONE_ALL);

    /*
     * 播放有限动画：
     * - CHASE_CLOCKWISE：绿色，BOTTOM -> LEFT -> TOP -> RIGHT，约 1 秒。
     * - CHASE_COUNTERCLOCKWISE：绿色，反向顺序，约 1 秒。
     * - FLASH：四灯绿色快速闪烁 3 次，周期约 180 ms。
     * - FADE_OUT：柔和琥珀色按 RIGHT -> TOP -> LEFT -> BOTTOM 渐暗，约 1 秒。
     * - NONE：等效 off()。
     *
     * 新视觉命令立即抢占动画；亮度命令不重置节拍。动画完成后熄灭，
     * 不恢复旧效果，也不保留最后一帧。
     */
    bool playAnimation(RgbAnimation animation);

    // 有限动画正在等待执行或运行时返回 true；持续灯效不算 busy。
    bool isBusy() const;

    // begin() 成功且尚未进入 end() 时返回 true。
    bool isBegun() const;

    // 渲染 task 已创建且尚未退出时返回 true。
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

    // _strip 只允许 RGB task 访问；每个 begin()/end() 周期独立创建和释放。
    Adafruit_NeoPixel* _strip = nullptr;
    QueueHandle_t _command_queue = nullptr;

    // 锁顺序固定为 api -> queue；RGB task 只获取 queue，避免强制退出遗留 api 锁。
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

    // 读取最新命令并按固定帧周期渲染；每个常规帧只调用一次 show()。
    void taskLoop();

    // 按值接收命令，以便注入最新亮度或合并待执行的亮度更新。
    bool sendCommand(Command command);

    // 仅由 RGB task 调用，切换当前效果并维护动画 busy 状态。
    void handleCommand(const Command& command);

    // 根据当前效果计算四颗像素；不直接调用 NeoPixel API。
    void renderFrame(uint32_t now, RgbColor pixels[RGB_LED_COUNT]);

    void setAnimationBusy(bool busy);
    void setTaskHandle(TaskHandle_t handle);
    void setTaskRunning(bool running);
    bool taskShouldRun() const;

    // 熄灭灯珠并释放 NeoPixel 在 ESP32 上占用的 RMT 资源。
    void releaseStripHardware();
};

#endif
