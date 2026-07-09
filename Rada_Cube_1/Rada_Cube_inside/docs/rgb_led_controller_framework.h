#ifndef RGB_LED_CONTROLLER_FRAMEWORK_H
#define RGB_LED_CONTROLLER_FRAMEWORK_H

/*
 * RgbLedController 第一版框架草稿
 *
 * 目标：
 * - 只做 RGB 硬件驱动和灯效渲染，不理解配对、唤醒、距离、失联等业务事件。
 * - 保留一个 RGB task 独占 NeoPixel 硬件，所有 public API 只投递命令。
 * - 命令队列长度为 1，新命令覆盖旧命令；灯光表示“当前状态”，不做排队回放。
 * - 对外 API 尽量朴素：传 enum，不暴露引脚组合、运算符重载或复杂状态结构体。
 *
 * LED 控制模型：
 * - blink/breathe 只维护一个节拍。
 * - 每个节拍到达时统一决定“这次要点亮还是熄灭”。
 * - 真正写像素时，再根据 LedZone 决定写四颗灯、三颗灯或单颗灯。
 */

#include <Arduino.h>

#define RGB_LED_COUNT                    4
#define RGB_LED_DEFAULT_BRIGHTNESS       64
#define RGB_LED_MAX_BRIGHTNESS           96
#define RGB_LED_FRAME_INTERVAL_MS        20
#define RGB_LED_TASK_STACK_WORDS         4096
#define RGB_LED_TASK_PRIORITY            1
#define RGB_LED_COMMAND_QUEUE_LENGTH     1

/*
 * 物理索引需要在真实硬件上确认。
 * 这里先沿用当前代码中的接线顺序。
 */
#define RGB_LED_INDEX_RIGHT              0
#define RGB_LED_INDEX_TOP                1
#define RGB_LED_INDEX_LEFT               2
#define RGB_LED_INDEX_BOTTOM             3

/*
 * 常用速度对应的周期。根据实际情况灵活调整数值
 * blink 和 breathe 可以复用同一个 RgbEffectSpeed enum，但具体周期分别定义。
 */
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
#define RGB_COLOR_ORANGE      RgbColor{255, 96, 0}
#define RGB_COLOR_YELLOW      RgbColor{255, 180, 0}
#define RGB_COLOR_GREEN       RgbColor{0, 255, 32}
#define RGB_COLOR_BLUE        RgbColor{0, 64, 255}
#define RGB_COLOR_WHITE       RgbColor{255, 255, 255}
#define RGB_COLOR_SOFT_AMBER  RgbColor{120, 42, 0}

/*
 * 固定灯区枚举，只描述物理选择。
 *
 * ONLY_LEFT/ONLY_RIGHT 用于单侧设备灯，例如单侧配对或唤醒成功。
 * LEFT/RIGHT 用于工作区域，例如左侧在线时左灯 + 上下灯一起显示距离。
 */
enum LedZone : uint8_t {
    LED_ZONE_NONE = 0,
    LED_ZONE_ALL,         // 四灯
    LED_ZONE_ONLY_LEFT,   // 仅左侧设备灯
    LED_ZONE_ONLY_RIGHT,  // 仅右侧设备灯
    LED_ZONE_LEFT,        // 左灯 + 上下灯
    LED_ZONE_RIGHT        // 右灯 + 上下灯
};

enum RgbEffectSpeed : uint8_t {
    RGB_EFFECT_SPEED_SLOW = 0,
    RGB_EFFECT_SPEED_MEDIUM,
    RGB_EFFECT_SPEED_FAST
};

/*
 * 物理动画原语。
 *
 * 不出现任何业务事件名字。
 * 具体业务事件要使用哪种物理动画，由 FeedbackController 决定。
 */
enum RgbAnimation : uint8_t {
    RGB_ANIMATION_NONE = 0,
    RGB_ANIMATION_CHASE_CLOCKWISE,
    RGB_ANIMATION_CHASE_COUNTERCLOCKWISE,
    RGB_ANIMATION_FLASH,
    RGB_ANIMATION_FADE_OUT
};

class RgbLedController {
public:
    RgbLedController();
    ~RgbLedController();

    /*
     * 初始化 NeoPixel、命令队列、状态同步对象，以及唯一的 RGB 渲染 task。
     * 初始化后默认关灯。
     */
    bool begin();

    /*
     * 停止渲染 task，清空像素，关闭 RGB 供电，并释放队列。
     */
    void end();

    /*
     * 设置默认亮度，范围 0..RGB_LED_MAX_BRIGHTNESS。
     * 实现里应做 clamp，避免调用方传入过大值。
     */
    bool setBrightness(uint8_t brightness);
    uint8_t brightness() const;

    /*
     * 关闭全部 LED。
     */
    bool off();

    /*
     * 普通灯效。
     *
     * solid 没有速度。
     * blink/breathe 使用 RgbEffectSpeed，不让上层传裸毫秒数。
     * 具体周期由 RGB_LED_BLINK_xxx_PERIOD_MS / RGB_LED_BREATHE_xxx_PERIOD_MS 决定。
     */
    bool solid(const RgbColor& color, LedZone zone = LED_ZONE_ALL);
    bool blink(const RgbColor& color,
               RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM,
               LedZone zone = LED_ZONE_ALL);
    bool breathe(const RgbColor& color,
                 RgbEffectSpeed speed = RGB_EFFECT_SPEED_MEDIUM,
                 LedZone zone = LED_ZONE_ALL);

    /*
     * 播放一个有限时长的物理动画。
     * 例如顺时针跑马灯、逆时针跑马灯、闪烁、淡出。
     */
    bool playAnimation(RgbAnimation animation);

    /*
     * 有限时长动画正在运行时返回 true。
     * 持续性的 solid/blink/breathe 表示当前状态，不算 busy。
     */
    bool isBusy() const;

    bool isBegun() const;
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

    /*
     * task 入口。
     * 具体 task、queue、NeoPixel 对象和状态变量，
     * 第一版框架不在头文件里展开，由实现同事根据当前代码补充。
     */
    static void taskEntry(void* arg);

    /*
     * taskLoop() 建议主流程：
     * 1. 从队列读取最新 Command；有新命令就调用 handleCommand() 更新当前效果。
     * 2. 按 RGB_LED_FRAME_INTERVAL_MS 计算是否需要刷新一帧。
     * 3. 根据当前命令类型和速度，统一决定本帧四颗灯的输出。
     * 4. 每一帧只调用一次 strip.show()。
     */
    void taskLoop();

    /*
     * 所有 public API 最终都构造 Command 并调用 sendCommand()。
     * sendCommand() 使用 xQueueOverwrite()，保证 task 只执行最新命令。
     * public API 不直接写像素。
     */
    bool sendCommand(const Command& command);

    /*
     * task 内部命令入口。
     * 这里处理 stop/solid/blink/breathe/animation/setBrightness 的状态切换。
     */
    void handleCommand(const Command& command);
};

/*
 * 最小实现策略
 *
 * - begin()/end()/taskEntry()/taskLoop()/NeoPixel 供电管理复用当前实现。
 * - public API 只保留 off/solid/blink/breathe/playAnimation。
 * - 不在 RGB 层出现 pair、wake、distance、lost、battery、fault 等业务命名。
 * - 不为左/右/上/下创建多个 task；一个 task 根据 LedZone 统一渲染四颗灯。
 * - LED_ZONE_LEFT/RIGHT 的具体像素集合在 .cpp 里 switch 判断，不暴露给调用方。
 */

#endif
