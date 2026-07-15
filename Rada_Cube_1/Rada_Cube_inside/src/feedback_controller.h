#ifndef FEEDBACK_CONTROLLER_H
#define FEEDBACK_CONTROLLER_H

/*
 * FeedbackController 第一版框架草稿
 *
 * 这个文件只定义“main 如何把业务事件交给反馈层”。
 * 不在第一版公开过多内部状态机、队列、token、去重 key，否则会让同事还没开始实现就被框架绑住。
 *
 * 核心边界：
 * - main 负责判断业务事件已经发生，例如左侧配对成功、双侧失联、距离等级变化。
 * - FeedbackController 负责把事件翻译成 RGB 灯效和声音。
 * - RgbLedController 只负责执行灯效。
 * - AudioCatalog 负责提供 AudioId 到路径的映射和倒车期间黑名单判断。
 * - SpeakerController 只负责播放指定路径的音频文件。
 *
 * 调用规则：
 * - 所有 onXxxEvent() 都必须立即返回，不在函数内部阻塞等待声音或动画结束。
 * - 需要等待的业务流程，通过 isBusy() + 超时判断完成。
 * - main 决定业务节奏：想保留前一个反馈，就先等 isBusy() 变 false；想抢占，就直接调用下一个事件。
 * - 除倒车距离显示外，main 调用新的 onXxxEvent() 就表示新事件可以替换/打断前一个视听反馈。
 * - 倒车距离显示期间不插入长语音，只允许短音效提示掉线、恢复等状态变化。
 * - 距离显示状态下，FeedbackController 只能用短提示音 playOnce() 打断距离 playLoop()。
 * - playOnce 打断 playLoop 后的 loop 恢复由 SpeakerController 负责，FeedbackController 不轮询恢复。
 * - 低优先级反馈，例如上电动画/启动音，可以被后续业务事件替换或打断。
 * - main 不需要询问“上电动画是否结束”再通知唤醒结果；直接调用对应事件即可。
 *
 * 将来移动到 src 后，正式代码预期包含：
 *   #include "rgb_led_controller.h"
 *   #include "speaker_controller.h"
 *   #include "audio_catalog.h"
 */

#include <Arduino.h>
#include "rgb_led_controller.h"
#include "speaker_controller.h"
#include "audio_catalog.h"

#define FEEDBACK_DEFAULT_TIMEOUT_MS       5000
#define FEEDBACK_EVENT_COOLDOWN_MS        5000

/*
 * FeedbackController 当前所处的业务场景。
 *
 * 第一版只用于统一休眠/关机事件，帮助 FeedbackController 决定是否需要先停止距离反馈、
 * 是否播放配对/工作场景下不同的退场灯效。
 */
enum class FeedbackScene : uint8_t {
    Idle,
    Unpaired,
    Pairing,
    Working
};

/*
 * 逻辑上的可用车外模块集合。
 *
 * 这不是物理 LED 区域。
 * 例如 SensorSet::Left 表示只有左侧外部模块可用；
 * 具体要亮哪几颗灯，由 FeedbackController 内部映射到 RgbLedController。
 */
enum class FeedbackSensorSet : uint8_t {
    None,
    Left,
    Right,
    Both
};

/*
 * 距离反馈等级。
 *
 * 第一版建议让 main 或现有距离逻辑继续负责判断距离等级，
 * FeedbackController 只根据等级选择灯效和蜂鸣音。
 * 等结构稳定后，再考虑把阈值判断也移动进来。
 */
enum class FeedbackDistanceLevel : uint8_t {
    Safe,       // 距离无效或 > 150 cm，静音
    VeryFar,    // 121..150 cm
    Far,        // 91..120 cm
    Medium,     // 61..90 cm
    Near,       // 31..60 cm
    Danger      // <= 30 cm
};

/*
 * 所有当前已经设计的反馈事件。
 *
 * 这个 enum 主要用于日志、调试和测试。
 * 正常业务代码优先调用下面具体的 onXxxEvent()，不要在 main 里 switch 这个 enum 做灯光和声音。
 *
 * 建模原则：
 * - 同一个触发原因，只是结果不同，用一个 event + 参数。
 * - 不要把 Left/Right/None 这种结果组合膨胀成多个 event。
 */
enum class FeedbackEvent : uint8_t {
    SystemBoot,
    Shutdown,

    UnpairedDetected,

    PairingStarted,
    PairLeftSucceeded,
    PairRightSucceeded,
    PairBothSucceeded,
    PairingTimedOut,

    WakeLeftSucceeded,
    WakeRightSucceeded,
    WakeTimedOut,

    DistanceLevelChanged,
    DistanceSensorFault,

    LeftLinkLost,
    RightLinkLost,
    BothLinksLost,
    LeftLinkRestored,
    RightLinkRestored
};

class FeedbackController {
public:
    /*
     * FeedbackController 不拥有硬件，只引用现有驱动。
     * rgb.begin() 和 speaker.begin() 以及end() 仍然由 setup() 或应用层负责调用。
     */
    FeedbackController(RgbLedController& rgb, SpeakerController& speaker);

    /*
     * 初始化反馈层自己的状态。
     * 不做硬件初始化，不进入具体业务模式。
     */
    bool begin();

    /*
     * 清理反馈层状态，停止当前灯效和声音。
     * 不销毁底层驱动对象。
     */
    void end();

    /*
     * 如果某些业务阶段需要等待最终提示音结束，例如配对完成、关机，
     * main 可以轮询这个函数。
     *
     * 第一版建议直接代理查询 SpeakerController/RgbLedController 的忙碌状态，
     * FeedbackController 不维护自己的异步调度队列。
     *
     * 注意：等待期间 main 仍然要继续处理无线接收和系统维护任务。
     */
    bool isBusy() const;

    /*
     * 调试用：返回最近一次被处理的事件。
     */
    FeedbackEvent lastEvent() const;

    // ---------------------------------------------------------------------
    // 通用事件
    // ---------------------------------------------------------------------

    /*
     * 系统上电。
     *
     * startup_scene 表示上电后预计进入的业务场景。
     * 例如配对模式可以完整播放配对提示；工作模式可以允许后续唤醒/距离事件快速打断。
     */
    void onSystemBootEvent(FeedbackScene startup_scene);

    /*
     * 统一休眠或关机事件。
     *
     * current_scene 表示当前从哪个业务场景退场。
     * 第一版只保留一个 shutdown 入口，不展开低电、故障、工作退出等原因分支。
     */
    void onShutdownEvent(FeedbackScene current_scene);

    // ---------------------------------------------------------------------
    // 未配对事件
    // ---------------------------------------------------------------------

    /*
     * 启动后检测到设备未配对。
     * 建议：保持安静，只做一次很轻的蓝色提示或短语音，避免反复打扰。
     */
    void onUnpairedDetectedEvent();

    // ---------------------------------------------------------------------
    // 配对事件
    // ---------------------------------------------------------------------

    /*
     * 进入配对模式。
     * 建议：四灯蓝色呼吸；无线配对流程立即开始，不等待声音。
     */
    void onPairingStartedEvent();

    /*
     * 左侧首先配对成功。
     * 建议：左侧单灯绿色常亮，播放一次短上升音，继续等待右侧。
     */
    void onPairLeftSucceededEvent();

    /*
     * 右侧首先配对成功。
     * 建议：右侧单灯绿色常亮，播放一次短上升音，继续等待左侧。
     */
    void onPairRightSucceededEvent();

    /*
     * Play one pairing-success tone without changing pairing state or LEDs.
     * main uses this event to serialize the two per-side success tones.
     */
    void onPairSuccessToneEvent();

    /*
     * 双侧配对成功。
     * 建议：四灯成功动画，播放一次最终配对成功提示。
     * main 可等待 isBusy() 变 false 后退出配对流程。
     */
    void onPairBothSucceededEvent();

    /*
     * 配对超时。
     *
     * paired_sensors 表示超时时已经配对成功的模块：
     * - None：双侧均失败，播放 pair_fail_both。
     * - Left：仅左侧成功，播放 pair_fail_right。
     * - Right：仅右侧成功，播放 pair_fail_left。
     *
     * 不应该传 Both；双侧都成功时应调用 onPairBothSucceededEvent()。
     */
    void onPairingTimedOutEvent(FeedbackSensorSet paired_sensors);

    // ---------------------------------------------------------------------
    // 唤醒事件
    // ---------------------------------------------------------------------

    /*
     * 左侧唤醒成功。
     * 左侧加入已唤醒集合并立即更新状态灯；成功提示音由 main 单独调度。
     */
    void onWakeLeftSucceededEvent();

    /*
     * 右侧唤醒成功。
     * 右侧加入已唤醒集合并立即更新状态灯；成功提示音由 main 单独调度。
     */
    void onWakeRightSucceededEvent();

    /*
     * Play one wake-success tone without changing wake state or LED state.
     * main uses this event to serialize multiple success tones while keeping
     * sensor state and LEDs responsive.
     */
    void onWakeSuccessToneEvent();

    /*
     * Play the final wake-completed prompt without changing wake state or LEDs.
     * main calls this after both per-side success tones have finished.
     */
    void onWakeCompletedToneEvent();

    /*
     * 唤醒超时。
     *
     * awake_sensors 表示超时时已经唤醒成功的模块：
     * - None：双侧均失败，进入系统不可用灯效，不启动距离蜂鸣。
     * - Left：仅左侧成功，右侧失败提示；是否单侧工作由 main 决定。
     * - Right：仅右侧成功，左侧失败提示；是否单侧工作由 main 决定。
     *
     * 不应该传 Both；双侧都唤醒成功时由左右两个 onWakeXxxSucceededEvent() 自动形成完成反馈。
     */
    void onWakeTimedOutEvent(FeedbackSensorSet awake_sensors);

    // ---------------------------------------------------------------------
    // 距离工作事件
    // ---------------------------------------------------------------------

    /*
     * 距离等级变化。
     *
     * 这个事件也承担“开始倒车距离显示”的职责。
     * main 收到第一帧有效距离后即可调用。
     * 如果业务上需要先播放 wake_ok，main 自己等待后再开始上报距离等级。
     *
     * active_sensors 用于决定物理灯区：
     * - Left：仅左灯显示距离。
     * - Right：仅右灯显示距离。
     * - Both：左、右两个灯显示距离。
     */
    void onDistanceLevelChangedEvent(FeedbackSensorSet active_sensors,
                                     FeedbackDistanceLevel level);

    /*
     * One or both distance sensors reached the consecutive-invalid limit.
     * The visual and audio feedback matches the corresponding link-loss
     * feedback, but main must keep the work mode running and wait for the
     * consecutive-valid recovery condition.
     */
    void onDistanceSensorFaultEvent(FeedbackSensorSet faulted_sensors);

    /*
     * 左侧确认失联。
     * 建议：左侧设备灯熄灭，剩余右侧继续距离反馈，播放一次短下降音。
     */
    void onLeftLinkLostEvent();

    /*
     * 右侧确认失联。
     * 建议：右侧设备灯熄灭，剩余左侧继续距离反馈，播放一次短下降音。
     */
    void onRightLinkLostEvent();

    /*
     * 双侧确认失联。
     * 建议：停止距离反馈，进入全局不可用灯效，播放一次合并故障短音。
     * 随后由 main 退出工作模式并进入休眠或关机流程，不设计自动恢复。
     */
    void onBothLinksLostEvent();

    /*
     * 左侧恢复在线。
     * 建议：左侧单灯绿色常亮，然后根据最新距离恢复距离反馈。
     */
    void onLeftLinkRestoredEvent();

    /*
     * 右侧恢复在线。
     * 建议：右侧单灯绿色常亮，然后根据最新距离恢复距离反馈。
     */
    void onRightLinkRestoredEvent();

private:
    RgbLedController& _rgb;
    SpeakerController& _speaker;

    FeedbackEvent _last_event = FeedbackEvent::SystemBoot;
    FeedbackScene _scene = FeedbackScene::Idle;
    FeedbackSensorSet _active_sensors = FeedbackSensorSet::None;
    FeedbackSensorSet _woke_sensors = FeedbackSensorSet::None;
    FeedbackDistanceLevel _distance_level = FeedbackDistanceLevel::Safe;
    // 判断是否需要在距离显示期间禁止长语音插入
    bool _distance_feedback_enabled = false;
};

/*
 * 第一版实现建议
 *
 * 1. 不实现 prompt queue，不实现 update() 轮询调度。
 *    需要顺序播放的场景由 main 通过 isBusy() 控制调用时机。
 *
 * 2. main 只调用事件 API，不直接写灯光和声音策略。
 *    例如 main 判断“左侧失联”后，只调用 onLeftLinkLostEvent()。
 *
 * 3. 每个 onXxxEvent() 内部先直接更新 _last_event 和必要状态，
 *    再组合调用 RgbLedController 和 SpeakerController。
 *    播放声音时，先选择 AudioId，再用 audio_path_from_id(audio_id) 得到路径，
 *    最后调用 _speaker.playOnce(path) 或 _speaker.playLoop(path)。
 *    第一版不预设额外私有 helper，避免还没实现就形成复杂框架。
 *
 * 4. 距离反馈先沿用当前代码已经有的等级和蜂鸣文件。
 *    playLoop 被 playOnce 打断后恢复 loop 的语义放在 SpeakerController。
 *    但“距离显示期间能不能播放某个音频”的判断放在 FeedbackController，
 *    可通过 audio_is_blocked_during_parking(audio_id) 拦截长语音。
 *
 * 5. 等这个简单版本跑通，再决定是否需要 token、去重 key、完整队列等更复杂机制。
 *    不要在第一版就把所有可能性都设计成公开类型。
 *
 *
 * 两个事件函数示例实现
 *
 * 下面代码建议放在 feedback_controller.cpp。
 * 目的不是一次性覆盖全部事件，而是给同事看清楚：
 * - onXxxEvent() 只更新必要状态。
 * - 直接把事件翻译成 RGB/Speaker 指令。
 * - 不在事件函数里等待音频或动画结束。
 *
 * 示例 1：左侧配对成功
 *
 * void FeedbackController::onPairLeftSucceededEvent()
 * {
 *     _last_event = FeedbackEvent::PairLeftSucceeded;
 *     _scene = FeedbackScene::Pairing;
 *     _active_sensors = FeedbackSensorSet::Left;
 *
 *     _rgb.solid(RGB_COLOR_GREEN, LED_ZONE_ONLY_LEFT);
 *
 *     const char *audio_path =
 *         audio_path_from_id(AUDIO_ID_TONE_SUCCESS_UP);
 *     if (audio_path != nullptr) {
 *         _speaker.playOnce(audio_path);
 *     }
 * }
 *
 * 示例 2：距离等级变化，也是开始倒车距离显示的入口
 *
 * void FeedbackController::onDistanceLevelChangedEvent(
 *     FeedbackSensorSet active_sensors,
 *     FeedbackDistanceLevel level)
 * {
 *     _last_event = FeedbackEvent::DistanceLevelChanged;
 *     _scene = FeedbackScene::Working;
 *     _active_sensors = active_sensors;
 *     _distance_level = level;
 *
 *     if (active_sensors == FeedbackSensorSet::None) {
 *         _distance_feedback_enabled = false;
 *         _rgb.off();
 *         _speaker.stop();
 *         return;
 *     }
 *
 *     _distance_feedback_enabled = true;
 *     LedZone zone = LED_ZONE_ALL;
 *     if (active_sensors == FeedbackSensorSet::Left) {
 *         zone = LED_ZONE_LEFT;
 *     } else if (active_sensors == FeedbackSensorSet::Right) {
 *         zone = LED_ZONE_RIGHT;
 *     }
 *
 *     AudioId audio_id = AUDIO_ID_NONE;
 *
 *     switch (level) {
 *     case FeedbackDistanceLevel::Safe:
 *         _rgb.solid(RGB_COLOR_GREEN, zone);
 *         _speaker.stop();
 *         return;
 *
 *     case FeedbackDistanceLevel::VeryFar:
 *         _rgb.blink(RGB_COLOR_GREEN, RGB_EFFECT_SPEED_SLOW, zone);
 *         audio_id = AUDIO_ID_BEEP_SLOW;
 *         break;
 *
 *     case FeedbackDistanceLevel::Far:
 *         _rgb.blink(RGB_COLOR_GREEN, RGB_EFFECT_SPEED_MEDIUM, zone);
 *         audio_id = AUDIO_ID_BEEP_MEDIUM_SLOW;
 *         break;
 *
 *     case FeedbackDistanceLevel::Medium:
 *         _rgb.blink(RGB_COLOR_YELLOW, RGB_EFFECT_SPEED_MEDIUM, zone);
 *         audio_id = AUDIO_ID_BEEP_MEDIUM;
 *         break;
 *
 *     case FeedbackDistanceLevel::Near:
 *         _rgb.blink(RGB_COLOR_RED, RGB_EFFECT_SPEED_FAST, zone);
 *         audio_id = AUDIO_ID_BEEP_FAST;
 *         break;
 *
 *     case FeedbackDistanceLevel::Danger:
 *         _rgb.solid(RGB_COLOR_RED, zone);
 *         audio_id = AUDIO_ID_BEEP_CONTINUOUS;
 *         break;
 *     }
 *
 *     const char *audio_path = audio_path_from_id(audio_id);
 *     if (audio_path != nullptr) {
 *         _speaker.playLoop(audio_path);
 *     }
 * }
 *
 *
 * main 调用 FeedbackController 的简单 demo
 *
 * RgbLedController rgb;
 * SpeakerController speaker;
 * FeedbackController feedback(rgb, speaker);
 *
 * void setup()
 * {
 *     rgb.begin();
 *     speaker.begin();
 *     feedback.begin();
 *
 *     // 工作模式上电：启动反馈可以被后续唤醒/距离事件打断。
 *     feedback.onSystemBootEvent(FeedbackScene::Working);
 * }
 *
 * void loop()
 * {
 *     // 示例：收到左侧配对成功 ACK。
 *     if (left_pair_ack_received) {
 *         feedback.onPairLeftSucceededEvent();
 *     }
 *
 *     // 示例：收到第一帧有效距离，或距离等级变化。
 *     if (distance_level_changed) {
 *         feedback.onDistanceLevelChangedEvent(
 *             FeedbackSensorSet::Both,
 *             FeedbackDistanceLevel::Medium);
 *     }
 *
 *     // 示例：需要等待最终提示时，main 自己决定是否等待。
 *     if (need_wait_final_prompt && !feedback.isBusy()) {
 *         enter_sleep_or_next_stage();
 *     }
 * }
 */

#endif
