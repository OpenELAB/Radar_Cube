#ifndef FEEDBACK_CONTROLLER_H
#define FEEDBACK_CONTROLLER_H

/*
 * FeedbackController first version framework draft
 *
 * This file only defines "how main delivers business events to the feedback layer".
 * Do not expose too many internal state machines, queues, tokens, and deduplication keys in the first version, otherwise colleagues will be tied to the framework before they even start implementing it.
 *
 * Core Boundary:
 * - main detects business events, such as left-sensor pairing, both links being lost, or a distance-level change.
 * - FeedbackController is responsible for translating events into RGB lighting effects and sounds.
 * - RgbLedController is only responsible for executing lighting effects.
 * - AudioCatalog is responsible for providing AudioId to path mapping and blacklist judgment during reversing.
 * - SpeakerController is only responsible for playing audio files in the specified path.
 *
 * Calling rules:
 * - All onXxxEvent() must return immediately and do not block inside the function waiting for the sound or animation to end.
 * - The business process that needs to wait is completed through isBusy() + timeout judgment.
 * - main controls the sequence: wait until isBusy() is false to retain the current feedback, or call the next event to interrupt it.
 * - In addition to the reversing distance display, calling the new onXxxEvent() in main means that the new event can replace/interrupt the previous audio-visual feedback.
 * - No long voice is inserted during the reversing distance display, only short sound effects are allowed to prompt status changes such as disconnection and recovery.
 * - In the distance display state, FeedbackController can only interrupt the distance playLoop() with a short beep playOnce().
 * - SpeakerController resumes playLoop after playOnce interrupts it; FeedbackController does not poll for resumption.
 * - Low-priority feedback, such as power-on animation/startup sound, can be replaced or interrupted by subsequent business events.
 * - main does not need to ask "whether the power-on animation is over" before notifying the wake-up result; just call the corresponding event directly.
 *
 * After moving to src in the future, the official code is expected to include:
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
 * The current business scenario of FeedbackController.
 *
 * The first version is only used to unify sleep/shutdown events to help FeedbackController decide whether to stop distance feedback first.
 * Whether to play different exit lighting effects in pairing/working scenes.
 */
enum class FeedbackScene : uint8_t {
    Idle,
    Unpaired,
    Pairing,
    Working
};

/*
 * Logical set of available off-vehicle modules.
 *
 * This is not a physical LED area.
 * For example, SensorSet::Left means that only the left external module is available;
 * The specific lights to be turned on are internally mapped to RgbLedController by FeedbackController.
 */
enum class FeedbackSensorSet : uint8_t {
    None,
    Left,
    Right,
    Both
};

/*
 * Distance feedback level.
 *
 * The first version recommends that main or existing distance logic continue to be responsible for determining distance levels.
 * FeedbackController only selects lighting effects and buzzers based on levels.
 * After the structure is stable, consider moving the threshold judgment into it.
 */
enum class FeedbackDistanceLevel : uint8_t {
    Safe,       // Invalid distance or > 150 cm, mute
    VeryFar,    // 121..150 cm
    Far,        // 91..120 cm
    Medium,     // 61..90 cm
    Near,       // 31..60 cm
    Danger      // <= 30 cm
};

/*
 * All currently designed feedback events.
 *
 * This enum is mainly used for logging, debugging and testing.
 * Normal business code gives priority to calling the specific onXxxEvent() below. Do not switch this enum in main to do lights and sounds.
 *
 * Modeling principles:
 * - The same trigger cause, but the result is different, using an event + parameter.
 * - Don’t expand Left/Right/None result combinations into multiple events.
 */
enum class FeedbackEvent : uint8_t {
    SystemBoot,
    Shutdown,

    UnpairedDetected,

    PairingStarted,
    // PairLeftSucceeded,
    // PairRightSucceeded,
    PairBothSucceeded,
    PairingTimedOut,

    // WakeLeftSucceeded,
    // WakeRightSucceeded,
    WakeTimedOut,

    DistanceLevelChanged,
    DistanceSensorFault,

    // LeftLinkLost,
    // RightLinkLost,
    BothLinksLost,
    // LeftLinkRestored,
    // RightLinkRestored,

    // The Unified Events API uses the following values
    PairSucceeded,
    WakeSucceeded,
    LinkLost,
    LinkRestored
};

class FeedbackController {
public:
    /*
     * FeedbackController does not own the hardware and only references the existing driver.
     * rgb.begin(), speaker.begin() and end() are still called by setup() or the application layer.
     */
    FeedbackController(RgbLedController& rgb, SpeakerController& speaker);

    /*
     * Initialize the feedback layer's own state.
     * No hardware initialization is performed, and no specific business mode is entered.
     */
    bool begin();

    /*
     * Clear the feedback layer status and stop the current lighting effects and sounds.
     * Do not destroy the underlying driver object.
     */
    void end();

    /*
     * If you need to wait for the final prompt sound to end in some business stages, such as pairing completion and shutdown,
     * main can poll this function.
     *
     * The first version recommends directly querying the busy status of SpeakerController/RgbLedController through proxy.
     * FeedbackController does not maintain its own asynchronous dispatch queue.
     *
     * Note: During the waiting period, main still needs to continue processing wireless reception and system maintenance tasks.
     */
    bool isBusy() const;

    /*
     * For debugging: Return the most recently processed event.
     * The Tone scheduling interface that only plays sound and does not change the service status does not update this value.
     */
    FeedbackEvent lastEvent() const;

    // ---------------------------------------------------------------------
    // General event
    // ---------------------------------------------------------------------

    /*
     * The system is powered on.
     *
     * startup_scene indicates the business scenario expected to be entered after power on.
     * For example, pairing mode can play the pairing prompt in full; working mode can allow subsequent wake/distance events to be quickly interrupted.
     */
    void onSystemBootEvent(FeedbackScene startup_scene);

    /*
     * Unified sleep or shutdown events.
     *
     * current_scene indicates which business scenario you are currently exiting from.
     * The first version only retains one shutdown entry, and does not expand branches due to low power, failure, job exit, etc.
     */
    void onShutdownEvent(FeedbackScene current_scene);

    // ---------------------------------------------------------------------
    // Unpaired event
    // ---------------------------------------------------------------------

    /*
     * It is detected that the device is not paired after startup.
     * Suggestion: Keep quiet and only make a very light blue prompt or short voice to avoid repeated interruptions.
     */
    void onUnpairedDetectedEvent();

    // ---------------------------------------------------------------------
    // pairing event
    // ---------------------------------------------------------------------

    /*
     * Enter pairing mode.
     * Recommendation: Four lights blue breathing; the wireless pairing process starts immediately without waiting for the sound.
     */
    void onPairingStartedEvent();

    /*
     * One or more modules are paired successfully.
     * paired_sensors indicates the set of modules that have been successfully paired after this status update.
     * The corresponding side light is always green; the success tone is scheduled separately by main.
     */
    void onPairSucceededEvent(FeedbackSensorSet paired_sensors);

    /*
     * Play the pairing success prompt once, and do not change the pairing status and LED status.
     * main uses this event to schedule two single-sided success prompts in sequence.
     * This audio-only scheduling command does not update lastEvent().
     */
    void onPairSuccessToneEvent();

    /*
     * Bilateral pairing is successful.
     * Suggestion: Four lights successful animation, play the final pairing success prompt once.
     * main can wait for isBusy() to become false before exiting the pairing process.
     */
    void onPairBothSucceededEvent();

    /*
     * Pairing timeout.
     *
     * paired_sensors indicates the modules that have been paired successfully during timeout:
     * - None: Both sides fail, play pair_fail_both.
     * - Left: Only the left side succeeds, play pair_fail_right.
     * - Right: Only the right side succeeds, play pair_fail_left.
     *
     * Both should not be passed; onPairBothSucceededEvent() should be called when both sides are successful.
     */
    void onPairingTimedOutEvent(FeedbackSensorSet paired_sensors);

    // ---------------------------------------------------------------------
    // Wake-up event
    // ---------------------------------------------------------------------

    /*
     * One or more modules wake up successfully.
     * awake_sensors represents the awakened collection after this status update; the success tone is scheduled separately by main.
     */
    void onWakeSucceededEvent(FeedbackSensorSet awake_sensors);

    /*
     * Play the wake-up success tone once, without changing the wake-up status and LED status.
     * main uses this event to schedule multiple success beeps in sequence while ensuring the sensor status and LED
     * Able to respond promptly.
     * This audio-only scheduling command does not update lastEvent().
     */
    void onWakeSuccessToneEvent();

    /*
     * Play the final wake-up completion prompt sound without changing the wake-up status and LED status.
     * main calls this event after the two single-sided success prompts have finished playing.
     * This audio-only scheduling command does not update lastEvent().
     * If distance feedback has started, intercept the prompt tone.
     */
    void onWakeCompletedToneEvent();

    /*
     * Wake-up timeout.
     *
     * awake_sensors indicates modules that have been successfully awakened during timeout:
     * - None: Both sides failed, the lighting effect is not available when entering the system, and the distance buzzer is not activated.
     * - Left: Only the left side succeeds, and the right side fails; whether one side works is determined by main.
     * - Right: Only the right side succeeds, and the left side fails; whether one side works is determined by main.
     *
     * Both should not be passed; when both sides wake up successfully, main should be called after the two unilateral success tones have ended.
     * onWakeCompletedToneEvent() forms the final completion feedback. This event will not be processed if Both is misrepresented.
     */
    void onWakeTimedOutEvent(FeedbackSensorSet awake_sensors);

    // ---------------------------------------------------------------------
    // Distance from work events
    // ---------------------------------------------------------------------

    /*
     * Distance level changes.
     *
     * This event also assumes the responsibility of "start reversing distance display".
     * main can be called after receiving the effective distance of the first frame.
     * If the business needs to play wake_ok first, main will wait before starting to report the distance level.
     *
     * active_sensors is used to determine the physical light area:
     * - Left: Only the left light displays the distance.
     * - Right: Only the right light displays the distance.
     * - Both: The left and right lights show the distance.
     */
    void onDistanceLevelChangedEvent(FeedbackSensorSet active_sensors,
                                     FeedbackDistanceLevel level);

    /*
     * One or two distance sensors have reached the upper limit of consecutive invalid times.
     * The light and sound feedback are consistent with the corresponding link loss feedback, but main must keep running in working mode.
     * And wait for the continuous valid frame recovery condition to be met.
     */
    void onDistanceSensorFaultEvent(FeedbackSensorSet faulted_sensors,
                                    FeedbackSensorSet active_sensors);

    /*
     * One-sided link is confirmed to be lost.
     * active_sensors represents the set of modules that can still participate in distance feedback after losing contact.
     * The remaining modules continue distance feedback and play a short falling tone.
     */
    void onLinkLostEvent(FeedbackSensorSet active_sensors);

    /*
     * Bilateral confirmation of loss of contact.
     * Suggestion: Stop distance feedback, enter the global unavailable lighting effect, and play a merge fault short sound.
     * Then main exits the working mode and enters the sleep or shutdown process, and automatic recovery is not designed.
     */
    void onBothLinksLostEvent();

    /*
     * One or more modules are back online.
     * active_sensors represents the set of modules that can participate in distance feedback after recovery.
     */
    void onLinkRestoredEvent(FeedbackSensorSet active_sensors);

private:
    RgbLedController& _rgb;
    SpeakerController& _speaker;

    FeedbackEvent _last_event = FeedbackEvent::SystemBoot;
    FeedbackScene _scene = FeedbackScene::Idle;
    FeedbackDistanceLevel _distance_level = FeedbackDistanceLevel::Safe;
    // Determine whether it is necessary to prohibit long voice insertion during distance display
    bool _distance_feedback_enabled = false;
};

/*
 * First version of implementation suggestions
 *
 * 1. Does not implement prompt queue and update() polling scheduling.
 *    Main controls the calling timing through isBusy() for scenes that need to be played sequentially.
 *
 * 2. main only calls the event API and does not write lighting and sound strategies directly.
 *    For example, after main determines that the link is lost, it only calls onLinkLostEvent(active_sensors).
 *
 * 3. Each business status onXxxEvent() directly updates _last_event and necessary status internally.
 *    Call RgbLedController and SpeakerController in combination.
 *    The Tone scheduling interface that only plays sound and does not change the business status does not update _last_event.
 *    When playing sound, first select AudioId, and then use audio_path_from_id(audio_id) to get the path.
 *    Finally call _speaker.playOnce(path) or _speaker.playLoop(path).
 *    The first version does not preset additional private helpers to avoid forming a complex framework before it is implemented.
 *
 * 4. The distance feedback first uses the existing level and beep files of the current code.
 *    The semantics of restoring the loop after playLoop is interrupted by playOnce are placed in SpeakerController.
 *    But the judgment of "whether a certain audio can be played during the distance display" is placed in FeedbackController.
 *    Long speech can be intercepted through audio_is_blocked_during_parking(audio_id).
 *
 * 5. Wait for this simple version to run through, and then decide whether more complex mechanisms such as tokens, deduplication keys, and complete queues are needed.
 *    Don’t design all possibilities as public types in the first version.
 *
 *
 * Example implementation of two event functions
 *
 * It is recommended that the following code be placed in feedback_controller.cpp.
 * The purpose is not to cover all events at once, but to let colleagues see clearly:
 * - onXxxEvent() only updates necessary status.
 * - Translate events directly into RGB/Speaker commands.
 * - Do not wait for the audio or animation to end in the event function.
 *
 * Example 1: Pairing status update
 *
 * void FeedbackController::onPairSucceededEvent(
 *     FeedbackSensorSet paired_sensors)
 * {
 *     _last_event = FeedbackEvent::PairSucceeded;
 *     _scene = FeedbackScene::Pairing;
 *
 *     _rgb.solid(RGB_COLOR_GREEN,
 *                sideZoneFromSensorSet(paired_sensors));
 *
 *     const char *audio_path =
 *         audio_path_from_id(AUDIO_ID_TONE_SUCCESS_UP);
 *     if (audio_path != nullptr) {
 *         _speaker.playOnce(audio_path);
 *     }
 * }
 *
 * Example 2: The distance level changes, which is also the entrance to start the reverse distance display.
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
 *         _rgb.blink(RGB_COLOR_YELLOW, RGB_EFFECT_SPEED_SLOW, zone);
 *         audio_id = AUDIO_ID_BEEP_MEDIUM_SLOW;
 *         break;
 *
 *     case FeedbackDistanceLevel::Medium:
 *         _rgb.blink(RGB_COLOR_ORANGE, RGB_EFFECT_SPEED_MEDIUM, zone);
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
 * Simple demo of main calling FeedbackController
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
 *     // Work mode power on: startup feedback can be interrupted by subsequent wake/distance events.
 *     feedback.onSystemBootEvent(FeedbackScene::Working);
 * }
 *
 * void loop()
 * {
 *     // Example: Received ACK of successful pairing from the left side.
 *     if (left_pair_ack_received) {
 *         feedback.onPairSucceededEvent(FeedbackSensorSet::Left);
 *     }
 *
 *     // Example: The effective distance of the first frame is received, or the distance level changes.
 *     if (distance_level_changed) {
 *         feedback.onDistanceLevelChangedEvent(
 *             FeedbackSensorSet::Both,
 *             FeedbackDistanceLevel::Medium);
 *     }
 *
 *     // Example: When it needs to wait for the final prompt, main decides whether to wait.
 *     if (need_wait_final_prompt && !feedback.isBusy()) {
 *         enter_sleep_or_next_stage();
 *     }
 * }
 */

#endif
