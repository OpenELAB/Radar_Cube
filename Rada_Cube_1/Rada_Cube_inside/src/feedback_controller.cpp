#include "feedback_controller.h"

namespace {

static const char* pathFromAudioId(AudioId audio_id)
{
    return audio_path_from_id(audio_id);
}

static void playOnceAudio(SpeakerController& speaker,
                          AudioId audio_id,
                          bool distance_feedback_enabled)
{
    if (distance_feedback_enabled &&
        audio_is_blocked_during_parking(audio_id)) {
        return;
    }

    const char* path = pathFromAudioId(audio_id);
    if (path != nullptr) {
        speaker.playOnce(path);
    }
}

static void playLoopAudio(SpeakerController& speaker, AudioId audio_id)
{
    const char* path = pathFromAudioId(audio_id);
    if (path != nullptr) {
        speaker.playLoop(path);
    } else {
        speaker.stop();
    }
}

static LedZone workZoneFromSensorSet(FeedbackSensorSet sensors)
{
    switch (sensors) {
    case FeedbackSensorSet::Left:
        return LED_ZONE_ONLY_LEFT;
    case FeedbackSensorSet::Right:
        return LED_ZONE_ONLY_RIGHT;
    case FeedbackSensorSet::Both:
        return LED_ZONE_ONLY_SIDES;
    case FeedbackSensorSet::None:
    default:
        return LED_ZONE_NONE;
    }
}

static LedZone sideZoneFromSensorSet(FeedbackSensorSet sensors)
{
    switch (sensors) {
    case FeedbackSensorSet::Left:
        return LED_ZONE_ONLY_LEFT;
    case FeedbackSensorSet::Right:
        return LED_ZONE_ONLY_RIGHT;
    case FeedbackSensorSet::Both:
        return LED_ZONE_ONLY_SIDES;
    case FeedbackSensorSet::None:
    default:
        return LED_ZONE_NONE;
    }
}

static FeedbackSensorSet addLeftSensor(FeedbackSensorSet sensors)
{
    switch (sensors) {
    case FeedbackSensorSet::None:
        return FeedbackSensorSet::Left;
    case FeedbackSensorSet::Right:
        return FeedbackSensorSet::Both;
    case FeedbackSensorSet::Left:
    case FeedbackSensorSet::Both:
    default:
        return sensors;
    }
}

static FeedbackSensorSet addRightSensor(FeedbackSensorSet sensors)
{
    switch (sensors) {
    case FeedbackSensorSet::None:
        return FeedbackSensorSet::Right;
    case FeedbackSensorSet::Left:
        return FeedbackSensorSet::Both;
    case FeedbackSensorSet::Right:
    case FeedbackSensorSet::Both:
    default:
        return sensors;
    }
}

static FeedbackSensorSet removeLeftSensor(FeedbackSensorSet sensors)
{
    switch (sensors) {
    case FeedbackSensorSet::Left:
        return FeedbackSensorSet::None;
    case FeedbackSensorSet::Both:
        return FeedbackSensorSet::Right;
    case FeedbackSensorSet::None:
    case FeedbackSensorSet::Right:
    default:
        return sensors;
    }
}

static FeedbackSensorSet removeRightSensor(FeedbackSensorSet sensors)
{
    switch (sensors) {
    case FeedbackSensorSet::Right:
        return FeedbackSensorSet::None;
    case FeedbackSensorSet::Both:
        return FeedbackSensorSet::Left;
    case FeedbackSensorSet::None:
    case FeedbackSensorSet::Left:
    default:
        return sensors;
    }
}

static void applyDistanceFeedback(RgbLedController& rgb,
                                  SpeakerController& speaker,
                                  FeedbackSensorSet sensors,
                                  FeedbackDistanceLevel level)
{
    if (sensors == FeedbackSensorSet::None) {
        rgb.off();
        speaker.stop();
        return;
    }

    const LedZone zone = workZoneFromSensorSet(sensors);
    AudioId audio_id = AUDIO_ID_NONE;

    switch (level) {
    case FeedbackDistanceLevel::Safe:
        rgb.solid(RGB_COLOR_GREEN, zone);
        speaker.stop();
        return;

    case FeedbackDistanceLevel::VeryFar:
        rgb.blink(RGB_COLOR_GREEN, RGB_EFFECT_SPEED_SLOW, zone);
        audio_id = AUDIO_ID_BEEP_SLOW;
        break;

    case FeedbackDistanceLevel::Far:
        rgb.blink(RGB_COLOR_GREEN, RGB_EFFECT_SPEED_MEDIUM, zone);
        audio_id = AUDIO_ID_BEEP_MEDIUM_SLOW;
        break;

    case FeedbackDistanceLevel::Medium:
        rgb.blink(RGB_COLOR_YELLOW, RGB_EFFECT_SPEED_MEDIUM, zone);
        audio_id = AUDIO_ID_BEEP_MEDIUM;
        break;

    case FeedbackDistanceLevel::Near:
        rgb.blink(RGB_COLOR_RED, RGB_EFFECT_SPEED_FAST, zone);
        audio_id = AUDIO_ID_BEEP_FAST;
        break;

    case FeedbackDistanceLevel::Danger:
        rgb.solid(RGB_COLOR_RED, zone);
        audio_id = AUDIO_ID_BEEP_CONTINUOUS;
        break;
    }

    playLoopAudio(speaker, audio_id);
}

} // namespace

FeedbackController::FeedbackController(RgbLedController& rgb,
                                       SpeakerController& speaker)
    : _rgb(rgb),
      _speaker(speaker)
{
}

bool FeedbackController::begin()
{
    _last_event = FeedbackEvent::SystemBoot;
    _scene = FeedbackScene::Idle;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _distance_level = FeedbackDistanceLevel::Safe;
    _distance_feedback_enabled = false;

    return _rgb.isBegun() && _speaker.isBegun();
}

void FeedbackController::end()
{
    _distance_feedback_enabled = false;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _scene = FeedbackScene::Idle;

    _speaker.stop();
    _rgb.off();
}

bool FeedbackController::isBusy() const
{
    return _rgb.isBusy() || _speaker.isBusy();
}

FeedbackEvent FeedbackController::lastEvent() const
{
    return _last_event;
}

void FeedbackController::onSystemBootEvent(FeedbackScene startup_scene)
{
    _last_event = FeedbackEvent::SystemBoot;
    _scene = startup_scene;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _distance_feedback_enabled = false;

    _speaker.stop();
    _rgb.playAnimation(RGB_ANIMATION_CHASE_CLOCKWISE);
    playOnceAudio(_speaker, AUDIO_ID_BOOT, false);
}

void FeedbackController::onShutdownEvent(FeedbackScene current_scene)
{
    _last_event = FeedbackEvent::Shutdown;
    _scene = current_scene;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _distance_feedback_enabled = false;

    _speaker.stop();
    _rgb.playAnimation(RGB_ANIMATION_FADE_OUT);
    playOnceAudio(_speaker, AUDIO_ID_SHUTDOWN, false);
}

void FeedbackController::onUnpairedDetectedEvent()
{
    _last_event = FeedbackEvent::UnpairedDetected;
    _scene = FeedbackScene::Unpaired;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _distance_feedback_enabled = false;

    _speaker.stop();
    _rgb.breathe(RGB_COLOR_BLUE,
                 RGB_EFFECT_SPEED_SLOW,
                 LED_ZONE_ALL);
    playOnceAudio(_speaker, AUDIO_ID_UNPAIRED, false);
}

void FeedbackController::onPairingStartedEvent()
{
    _last_event = FeedbackEvent::PairingStarted;
    _scene = FeedbackScene::Pairing;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _distance_feedback_enabled = false;

    _speaker.stop();
    _rgb.breathe(RGB_COLOR_BLUE,
                 RGB_EFFECT_SPEED_MEDIUM,
                 LED_ZONE_ALL);
    playOnceAudio(_speaker, AUDIO_ID_PAIRING, false);
}

void FeedbackController::onPairLeftSucceededEvent()
{
    _last_event = FeedbackEvent::PairLeftSucceeded;
    _scene = FeedbackScene::Pairing;
    _active_sensors = addLeftSensor(_active_sensors);

    _rgb.solid(RGB_COLOR_GREEN,
               sideZoneFromSensorSet(_active_sensors));
}

void FeedbackController::onPairRightSucceededEvent()
{
    _last_event = FeedbackEvent::PairRightSucceeded;
    _scene = FeedbackScene::Pairing;
    _active_sensors = addRightSensor(_active_sensors);

    _rgb.solid(RGB_COLOR_GREEN,
               sideZoneFromSensorSet(_active_sensors));
}

void FeedbackController::onPairSuccessToneEvent()
{
    playOnceAudio(_speaker, AUDIO_ID_TONE_SUCCESS_UP, false);
}

void FeedbackController::onPairBothSucceededEvent()
{
    _last_event = FeedbackEvent::PairBothSucceeded;
    _scene = FeedbackScene::Pairing;
    _active_sensors = FeedbackSensorSet::Both;
    _distance_feedback_enabled = false;

    _rgb.playAnimation(RGB_ANIMATION_FLASH);
    playOnceAudio(_speaker, AUDIO_ID_PAIR_OK_BOTH, false);
}

void FeedbackController::onPairingTimedOutEvent(
    FeedbackSensorSet paired_sensors)
{
    if (paired_sensors == FeedbackSensorSet::Both) {
        onPairBothSucceededEvent();
        return;
    }

    _last_event = FeedbackEvent::PairingTimedOut;
    _scene = FeedbackScene::Pairing;
    _active_sensors = paired_sensors;
    _distance_feedback_enabled = false;

    _speaker.stop();

    AudioId audio_id = AUDIO_ID_PAIR_FAIL_BOTH;
    switch (paired_sensors) {
    case FeedbackSensorSet::Left:
        _rgb.blink(RGB_COLOR_ORANGE,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ONLY_RIGHT);
        audio_id = AUDIO_ID_PAIR_FAIL_RIGHT;
        break;

    case FeedbackSensorSet::Right:
        _rgb.blink(RGB_COLOR_ORANGE,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ONLY_LEFT);
        audio_id = AUDIO_ID_PAIR_FAIL_LEFT;
        break;

    case FeedbackSensorSet::None:
    default:
        _rgb.blink(RGB_COLOR_RED,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ALL);
        audio_id = AUDIO_ID_PAIR_FAIL_BOTH;
        break;
    }

    playOnceAudio(_speaker, audio_id, false);
}

void FeedbackController::onWakeLeftSucceededEvent()
{
    _last_event = FeedbackEvent::WakeLeftSucceeded;
    _scene = FeedbackScene::Working;
    _woke_sensors = addLeftSensor(_woke_sensors);
    _active_sensors = _woke_sensors;

    _rgb.solid(RGB_COLOR_GREEN,
               sideZoneFromSensorSet(_woke_sensors));
}

void FeedbackController::onWakeRightSucceededEvent()
{
    _last_event = FeedbackEvent::WakeRightSucceeded;
    _scene = FeedbackScene::Working;
    _woke_sensors = addRightSensor(_woke_sensors);
    _active_sensors = _woke_sensors;

    _rgb.solid(RGB_COLOR_GREEN,
               sideZoneFromSensorSet(_woke_sensors));
}

void FeedbackController::onWakeSuccessToneEvent()
{
    playOnceAudio(_speaker, AUDIO_ID_TONE_SUCCESS_UP, false);
}

void FeedbackController::onWakeCompletedToneEvent()
{
    playOnceAudio(_speaker,
                  AUDIO_ID_WAKE_OK,
                  _distance_feedback_enabled);
}

void FeedbackController::onWakeTimedOutEvent(
    FeedbackSensorSet awake_sensors)
{
    // Both 表示唤醒完成，而不是唤醒超时。忽略这个非法调用，避免在给出
    // 成功反馈的同时，将最近一次事件错误地覆盖为 WakeTimedOut。
    if (awake_sensors == FeedbackSensorSet::Both) {
        return;
    }

    _last_event = FeedbackEvent::WakeTimedOut;
    _scene = FeedbackScene::Working;
    _woke_sensors = awake_sensors;
    _active_sensors = awake_sensors;
    _distance_feedback_enabled = false;

    _speaker.stop();

    switch (awake_sensors) {
    case FeedbackSensorSet::Left:
        _rgb.blink(RGB_COLOR_ORANGE,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ONLY_RIGHT);
        playOnceAudio(_speaker, AUDIO_ID_TONE_WARNING_DOWN, false);
        break;

    case FeedbackSensorSet::Right:
        _rgb.blink(RGB_COLOR_ORANGE,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ONLY_LEFT);
        playOnceAudio(_speaker, AUDIO_ID_TONE_WARNING_DOWN, false);
        break;

    case FeedbackSensorSet::None:
    case FeedbackSensorSet::Both:
    default:
        _rgb.blink(RGB_COLOR_RED,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ALL);
        playOnceAudio(_speaker, AUDIO_ID_CONNECTION_LOST, false);
        break;
    }
}

void FeedbackController::onDistanceLevelChangedEvent(
    FeedbackSensorSet active_sensors,
    FeedbackDistanceLevel level)
{
    _last_event = FeedbackEvent::DistanceLevelChanged;
    _scene = FeedbackScene::Working;
    _active_sensors = active_sensors;
    _distance_level = level;
    _distance_feedback_enabled =
        active_sensors != FeedbackSensorSet::None;

    applyDistanceFeedback(_rgb,
                          _speaker,
                          _active_sensors,
                          _distance_level);
}

void FeedbackController::onDistanceSensorFaultEvent(
    FeedbackSensorSet faulted_sensors)
{
    _last_event = FeedbackEvent::DistanceSensorFault;
    _scene = FeedbackScene::Working;

    switch (faulted_sensors) {
    case FeedbackSensorSet::Left:
        _active_sensors = removeLeftSensor(_active_sensors);

        if (_distance_feedback_enabled) {
            applyDistanceFeedback(_rgb,
                                  _speaker,
                                  _active_sensors,
                                  _distance_level);
            if (_active_sensors == FeedbackSensorSet::None) {
                _distance_feedback_enabled = false;
            }
        } else {
            const LedZone zone = sideZoneFromSensorSet(_active_sensors);
            if (zone == LED_ZONE_NONE) {
                _rgb.off();
            } else {
                _rgb.solid(RGB_COLOR_GREEN, zone);
            }
        }

        playOnceAudio(_speaker,
                      AUDIO_ID_TONE_WARNING_DOWN,
                      _distance_feedback_enabled);
        break;

    case FeedbackSensorSet::Right:
        _active_sensors = removeRightSensor(_active_sensors);

        if (_distance_feedback_enabled) {
            applyDistanceFeedback(_rgb,
                                  _speaker,
                                  _active_sensors,
                                  _distance_level);
            if (_active_sensors == FeedbackSensorSet::None) {
                _distance_feedback_enabled = false;
            }
        } else {
            const LedZone zone = sideZoneFromSensorSet(_active_sensors);
            if (zone == LED_ZONE_NONE) {
                _rgb.off();
            } else {
                _rgb.solid(RGB_COLOR_GREEN, zone);
            }
        }

        playOnceAudio(_speaker,
                      AUDIO_ID_TONE_WARNING_DOWN,
                      _distance_feedback_enabled);
        break;

    case FeedbackSensorSet::Both:
        _active_sensors = FeedbackSensorSet::None;
        _distance_feedback_enabled = false;

        _speaker.stop();
        _rgb.blink(RGB_COLOR_RED,
                   RGB_EFFECT_SPEED_FAST,
                   LED_ZONE_ALL);
        playOnceAudio(_speaker, AUDIO_ID_CONNECTION_LOST, false);
        break;

    case FeedbackSensorSet::None:
    default:
        break;
    }
}

void FeedbackController::onLeftLinkLostEvent()
{
    _last_event = FeedbackEvent::LeftLinkLost;
    _scene = FeedbackScene::Working;
    _active_sensors = removeLeftSensor(_active_sensors);

    if (_distance_feedback_enabled) {
        applyDistanceFeedback(_rgb,
                              _speaker,
                              _active_sensors,
                              _distance_level);
        if (_active_sensors == FeedbackSensorSet::None) {
            _distance_feedback_enabled = false;
        }
    } else {
        const LedZone zone = sideZoneFromSensorSet(_active_sensors);
        if (zone == LED_ZONE_NONE) {
            _rgb.off();
        } else {
            _rgb.solid(RGB_COLOR_GREEN, zone);
        }
    }

    playOnceAudio(_speaker,
                  AUDIO_ID_TONE_WARNING_DOWN,
                  _distance_feedback_enabled);
}

void FeedbackController::onRightLinkLostEvent()
{
    _last_event = FeedbackEvent::RightLinkLost;
    _scene = FeedbackScene::Working;
    _active_sensors = removeRightSensor(_active_sensors);

    if (_distance_feedback_enabled) {
        applyDistanceFeedback(_rgb,
                              _speaker,
                              _active_sensors,
                              _distance_level);
        if (_active_sensors == FeedbackSensorSet::None) {
            _distance_feedback_enabled = false;
        }
    } else {
        const LedZone zone = sideZoneFromSensorSet(_active_sensors);
        if (zone == LED_ZONE_NONE) {
            _rgb.off();
        } else {
            _rgb.solid(RGB_COLOR_GREEN, zone);
        }
    }

    playOnceAudio(_speaker,
                  AUDIO_ID_TONE_WARNING_DOWN,
                  _distance_feedback_enabled);
}

void FeedbackController::onBothLinksLostEvent()
{
    _last_event = FeedbackEvent::BothLinksLost;
    _scene = FeedbackScene::Working;
    _active_sensors = FeedbackSensorSet::None;
    _woke_sensors = FeedbackSensorSet::None;
    _distance_feedback_enabled = false;

    _speaker.stop();
    _rgb.blink(RGB_COLOR_RED,
               RGB_EFFECT_SPEED_FAST,
               LED_ZONE_ALL);
    playOnceAudio(_speaker, AUDIO_ID_CONNECTION_LOST, false);
}

void FeedbackController::onLeftLinkRestoredEvent()
{
    _last_event = FeedbackEvent::LeftLinkRestored;
    _scene = FeedbackScene::Working;
    _active_sensors = addLeftSensor(_active_sensors);

    if (_distance_feedback_enabled) {
        applyDistanceFeedback(_rgb,
                              _speaker,
                              _active_sensors,
                              _distance_level);
    } else {
        _rgb.solid(RGB_COLOR_GREEN,
                   sideZoneFromSensorSet(_active_sensors));
    }

    playOnceAudio(_speaker,
                  AUDIO_ID_TONE_SUCCESS_UP,
                  _distance_feedback_enabled);
}

void FeedbackController::onRightLinkRestoredEvent()
{
    _last_event = FeedbackEvent::RightLinkRestored;
    _scene = FeedbackScene::Working;
    _active_sensors = addRightSensor(_active_sensors);

    if (_distance_feedback_enabled) {
        applyDistanceFeedback(_rgb,
                              _speaker,
                              _active_sensors,
                              _distance_level);
    } else {
        _rgb.solid(RGB_COLOR_GREEN,
                   sideZoneFromSensorSet(_active_sensors));
    }

    playOnceAudio(_speaker,
                  AUDIO_ID_TONE_SUCCESS_UP,
                  _distance_feedback_enabled);
}
