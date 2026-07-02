#include "rgb_led_controller.h"

namespace {

constexpr uint8_t RGB_LED_POWER_ENABLE_LEVEL = HIGH;
constexpr uint8_t RGB_LED_POWER_DISABLE_LEVEL = LOW;
constexpr uint32_t RGB_LED_POWER_SETTLE_DELAY_MS = 5;

constexpr uint16_t PAIR_FAST_BLINK_PERIOD_MS = 180;
constexpr uint16_t UNPAIRED_BLINK_PERIOD_MS = 300;
constexpr uint16_t PAIRING_BLINK_PERIOD_MS = 1200;
constexpr uint16_t CONNECTED_PULSE_PERIOD_MS = 220;
constexpr uint16_t CONNECTED_PULSE_ON_MS = 110;
constexpr uint16_t CONNECTED_PULSE_TOTAL_MS = CONNECTED_PULSE_PERIOD_MS * 2;
constexpr uint16_t FAULT_FLASH_PERIOD_MS = 260;
constexpr uint16_t FAULT_FLASH_TOTAL_MS = FAULT_FLASH_PERIOD_MS * 2;
constexpr uint16_t LOST_INITIAL_PERIOD_MS = 1000;
constexpr uint16_t LOST_INITIAL_TOTAL_MS = 6000;
constexpr uint16_t LOST_SLOW_PERIOD_MS = 3000;
constexpr uint16_t LOW_BATTERY_PERIOD_MS = 2200;
constexpr uint16_t STARTUP_SWEEP_MS = 1000;
constexpr uint16_t STARTUP_FINAL_BLINK_MS = 200;
constexpr uint16_t STANDBY_FADE_MS = 1000;

void setRgbPower(bool enabled)
{
    pinMode(RGB_LED_PWR_PIN, OUTPUT);
    digitalWrite(
        RGB_LED_PWR_PIN,
        enabled ? RGB_LED_POWER_ENABLE_LEVEL : RGB_LED_POWER_DISABLE_LEVEL);
}

uint8_t brightnessValue(RgbBrightnessLevel level)
{
    switch (level) {
    case RgbBrightnessLevel::Low:
        return RGB_LED_BRIGHTNESS_LOW_VALUE;
    case RgbBrightnessLevel::Medium:
        return RGB_LED_BRIGHTNESS_MED_VALUE;
    case RgbBrightnessLevel::High:
        return RGB_LED_BRIGHTNESS_HIGH_VALUE;
    case RgbBrightnessLevel::Default:
    default:
        return RGB_LED_DEFAULT_BRIGHTNESS;
    }
}

uint8_t applyLevel(uint8_t value, uint8_t level)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * level) / 255);
}

RgbColor scaleColor(const RgbColor& color, uint8_t level)
{
    return RgbColor{
        applyLevel(color.r, level),
        applyLevel(color.g, level),
        applyLevel(color.b, level)
    };
}

uint8_t triangleWave(uint32_t elapsed_ms, uint16_t period_ms)
{
    if (period_ms == 0) {
        return 255;
    }

    const uint32_t phase = elapsed_ms % period_ms;
    const uint32_t half_period = period_ms / 2;

    if (half_period == 0) {
        return 255;
    }

    if (phase < half_period) {
        return static_cast<uint8_t>((phase * 255) / half_period);
    }

    return static_cast<uint8_t>(((period_ms - phase) * 255) / (period_ms - half_period));
}

bool blinkOn(uint32_t elapsed_ms, uint16_t period_ms)
{
    if (period_ms <= 1) {
        return true;
    }
    return (elapsed_ms % period_ms) < (period_ms / 2);
}

void clearPixels(RgbColor pixels[RGB_LED_COUNT])
{
    for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
        pixels[i] = RGB_COLOR_BLACK;
    }
}

void fillPixels(RgbColor pixels[RGB_LED_COUNT], const RgbColor& color)
{
    for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
        pixels[i] = color;
    }
}

bool isOneShotMode(RgbLedMode mode)
{
    return mode == RgbLedMode::Startup ||
           mode == RgbLedMode::Standby ||
           mode == RgbLedMode::UnpairedWarning ||
           mode == RgbLedMode::PairSuccess ||
           mode == RgbLedMode::FactoryReset;
}

} // namespace

RgbLedController::RgbLedController()
    : _strip(RGB_LED_COUNT, RGB_LED_PIN, RGB_LED_PIXEL_TYPE)
{
    _current_command.mode = RgbLedMode::Off;
    _current_command.color = RGB_COLOR_BLACK;
    _current_command.brightness = RgbBrightnessLevel::Default;
    _current_command.period_ms = 1000;
}

RgbLedController::~RgbLedController()
{
    end();
}

bool RgbLedController::begin()
{
    if (_begun) {
        return true;
    }

    _command_queue = xQueueCreate(RGB_LED_COMMAND_QUEUE_LENGTH, sizeof(RgbLedCommand));
    if (_command_queue == nullptr) {
        return false;
    }

    setRgbPower(true);
    delay(RGB_LED_POWER_SETTLE_DELAY_MS);

    setTaskRunning(true);
    setEffectFinished(true);
    _effect_start_ms = millis();
    _last_render_ms = 0;

    TaskHandle_t created_handle = nullptr;
    const BaseType_t created = xTaskCreate(
        taskEntry,
        "rgb_led",
        RGB_LED_TASK_STACK_WORDS,
        this,
        RGB_LED_TASK_PRIORITY,
        &created_handle);

    if (created != pdPASS) {
        setTaskRunning(false);
        setTaskHandle(nullptr);
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        setRgbPower(false);
        return false;
    }

    setTaskHandle(created_handle);
    _begun = true;
    return true;
}

void RgbLedController::end()
{
    if (!_begun) {
        return;
    }

    off();
    setTaskRunning(false);

    const uint32_t wait_start = millis();
    while (taskHandle() != nullptr && millis() - wait_start < 200) {
        vTaskDelay(pdMS_TO_TICKS(RGB_LED_FRAME_INTERVAL_MS));
    }

    TaskHandle_t handle = taskHandle();
    if (handle != nullptr) {
        vTaskDelete(handle);
        setTaskHandle(nullptr);
    }

    setRgbPower(false);

    if (_command_queue != nullptr) {
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
    }

    setCurrentMode(RgbLedMode::Off);
    setEffectFinished(true);
    _begun = false;
}

bool RgbLedController::setBrightness(RgbBrightnessLevel brightness)
{
    if (brightnessValue(brightness) > RGB_LED_MAX_BRIGHTNESS) {
        return false;
    }

    taskENTER_CRITICAL(&_state_mux);
    _current_brightness = brightness;
    taskEXIT_CRITICAL(&_state_mux);
    return true;
}

RgbBrightnessLevel RgbLedController::brightness() const
{
    return currentBrightness();
}

bool RgbLedController::off()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Off;
    command.color = RGB_COLOR_BLACK;
    return sendCommand(command);
}

bool RgbLedController::solid(const RgbColor& color)
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Solid;
    command.color = color;
    return sendCommand(command);
}

bool RgbLedController::blink(const RgbColor& color, uint16_t period_ms)
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Blink;
    command.color = color;
    command.period_ms = period_ms == 0 ? 1 : period_ms;
    return sendCommand(command);
}

bool RgbLedController::breathe(const RgbColor& color, uint16_t period_ms)
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Breathe;
    command.color = color;
    command.period_ms = period_ms == 0 ? 1 : period_ms;
    return sendCommand(command);
}

bool RgbLedController::startup()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Startup;
    return sendCommand(command);
}

bool RgbLedController::standby()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Standby;
    return sendCommand(command);
}

bool RgbLedController::unpairedWarning()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::UnpairedWarning;
    return sendCommand(command);
}

bool RgbLedController::pairing()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::Pairing;
    return sendCommand(command);
}

bool RgbLedController::pairSuccess()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::PairSuccess;
    return sendCommand(command);
}

bool RgbLedController::factoryReset()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::FactoryReset;
    return sendCommand(command);
}

bool RgbLedController::showSystemStatus()
{
    RgbLedCommand command;
    command.mode = RgbLedMode::SystemStatus;
    return sendCommand(command);
}

bool RgbLedController::setSensorConnected(RgbSensorSide side, bool connected)
{
    RgbLedCommand command;
    command.action = RgbLedAction::SetConnected;
    command.side = side;
    command.value = connected;
    return sendCommand(command);
}

bool RgbLedController::sensorConnectedPulse(RgbSensorSide side)
{
    RgbLedCommand command;
    command.action = RgbLedAction::ConnectedPulse;
    command.side = side;
    return sendCommand(command);
}

bool RgbLedController::setSensorLost(RgbSensorSide side, bool lost)
{
    RgbLedCommand command;
    command.action = RgbLedAction::SetLost;
    command.side = side;
    command.value = lost;
    return sendCommand(command);
}

bool RgbLedController::setSensorLowBattery(RgbSensorSide side, bool low)
{
    RgbLedCommand command;
    command.action = RgbLedAction::SetLowBattery;
    command.side = side;
    command.value = low;
    return sendCommand(command);
}

bool RgbLedController::setInsideLowBattery(bool low)
{
    RgbLedCommand command;
    command.action = RgbLedAction::SetInsideLowBattery;
    command.value = low;
    return sendCommand(command);
}

bool RgbLedController::setSensorFault(RgbSensorSide side, bool fault)
{
    RgbLedCommand command;
    command.action = RgbLedAction::SetFault;
    command.side = side;
    command.value = fault;
    return sendCommand(command);
}

bool RgbLedController::updateParkingDistance(uint16_t distance_cm)
{
    RgbLedCommand command;
    command.action = RgbLedAction::UpdateParkingDistance;
    command.distance_cm = distance_cm;
    return sendCommand(command);
}

bool RgbLedController::clearParkingDistance()
{
    RgbLedCommand command;
    command.action = RgbLedAction::ClearParkingDistance;
    return sendCommand(command);
}

bool RgbLedController::effectFinished() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool finished = _effect_finished;
    taskEXIT_CRITICAL(&_state_mux);
    return finished;
}

bool RgbLedController::isBegun() const
{
    return _begun;
}

bool RgbLedController::isTaskRunning() const
{
    return taskShouldRun() && taskHandle() != nullptr;
}

RgbLedMode RgbLedController::currentMode() const
{
    taskENTER_CRITICAL(&_state_mux);
    const RgbLedMode mode = _current_mode;
    taskEXIT_CRITICAL(&_state_mux);
    return mode;
}

void RgbLedController::setTaskRunning(bool running)
{
    taskENTER_CRITICAL(&_state_mux);
    _task_running = running;
    taskEXIT_CRITICAL(&_state_mux);
}

bool RgbLedController::taskShouldRun() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool running = _task_running;
    taskEXIT_CRITICAL(&_state_mux);
    return running;
}

TaskHandle_t RgbLedController::taskHandle() const
{
    taskENTER_CRITICAL(&_state_mux);
    TaskHandle_t handle = _task_handle;
    taskEXIT_CRITICAL(&_state_mux);
    return handle;
}

void RgbLedController::setTaskHandle(TaskHandle_t handle)
{
    taskENTER_CRITICAL(&_state_mux);
    _task_handle = handle;
    taskEXIT_CRITICAL(&_state_mux);
}

void RgbLedController::setCurrentMode(RgbLedMode mode)
{
    taskENTER_CRITICAL(&_state_mux);
    _current_mode = mode;
    taskEXIT_CRITICAL(&_state_mux);
}

void RgbLedController::setEffectFinished(bool finished)
{
    taskENTER_CRITICAL(&_state_mux);
    _effect_finished = finished;
    taskEXIT_CRITICAL(&_state_mux);
}

RgbBrightnessLevel RgbLedController::currentBrightness() const
{
    taskENTER_CRITICAL(&_state_mux);
    const RgbBrightnessLevel brightness = _current_brightness;
    taskEXIT_CRITICAL(&_state_mux);
    return brightness;
}

void RgbLedController::taskEntry(void* arg)
{
    auto* controller = static_cast<RgbLedController*>(arg);
    controller->taskLoop();
    controller->setTaskHandle(nullptr);
    vTaskDelete(nullptr);
}

void RgbLedController::taskLoop()
{
    _strip.begin();
    _strip.setBrightness(brightnessValue(currentBrightness()));
    _strip.clear();
    _strip.show();

    while (taskShouldRun()) {
        RgbLedCommand new_command;
        while (xQueueReceive(_command_queue, &new_command, 0) == pdTRUE) {
            const uint32_t now = millis();

            switch (new_command.action) {
            case RgbLedAction::SetMode:
                _current_command = new_command;
                setCurrentMode(new_command.mode);
                setEffectFinished(!isOneShotMode(new_command.mode));
                _effect_start_ms = now;
                if (new_command.mode == RgbLedMode::Off) {
                    _runtime_status.parking_active = false;
                } else if (new_command.mode == RgbLedMode::Pairing) {
                    _runtime_status.left_lost = false;
                    _runtime_status.right_lost = false;
                    _runtime_status.left_fault = false;
                    _runtime_status.right_fault = false;
                    _runtime_status.left_low_battery = false;
                    _runtime_status.right_low_battery = false;
                    _runtime_status.parking_active = false;
                }
                break;

            case RgbLedAction::SetConnected:
                if (new_command.side == RgbSensorSide::Left) {
                    const bool was_connected = _runtime_status.left_connected;
                    _runtime_status.left_connected = new_command.value;
                    if (new_command.value) {
                        _runtime_status.left_lost = false;
                        if (!was_connected) {
                            _runtime_status.left_connected_pulse_start_ms = now;
                        }
                    } else {
                        _runtime_status.left_connected_pulse_start_ms = 0;
                    }
                } else {
                    const bool was_connected = _runtime_status.right_connected;
                    _runtime_status.right_connected = new_command.value;
                    if (new_command.value) {
                        _runtime_status.right_lost = false;
                        if (!was_connected) {
                            _runtime_status.right_connected_pulse_start_ms = now;
                        }
                    } else {
                        _runtime_status.right_connected_pulse_start_ms = 0;
                    }
                }
                if (_current_mode != RgbLedMode::ParkingDistance &&
                    _current_mode != RgbLedMode::Pairing) {
                    setCurrentMode(RgbLedMode::SystemStatus);
                    setEffectFinished(true);
                }
                break;

            case RgbLedAction::ConnectedPulse:
                if (new_command.side == RgbSensorSide::Left) {
                    _runtime_status.left_connected = true;
                    _runtime_status.left_lost = false;
                    _runtime_status.left_connected_pulse_start_ms = now;
                } else {
                    _runtime_status.right_connected = true;
                    _runtime_status.right_lost = false;
                    _runtime_status.right_connected_pulse_start_ms = now;
                }
                if (_current_mode != RgbLedMode::ParkingDistance &&
                    _current_mode != RgbLedMode::Pairing) {
                    setCurrentMode(RgbLedMode::SystemStatus);
                    setEffectFinished(true);
                }
                break;

            case RgbLedAction::SetLost: {
                bool* lost = new_command.side == RgbSensorSide::Left
                    ? &_runtime_status.left_lost
                    : &_runtime_status.right_lost;
                bool* connected = new_command.side == RgbSensorSide::Left
                    ? &_runtime_status.left_connected
                    : &_runtime_status.right_connected;
                uint32_t* lost_start = new_command.side == RgbSensorSide::Left
                    ? &_runtime_status.left_lost_start_ms
                    : &_runtime_status.right_lost_start_ms;
                if (*lost != new_command.value) {
                    *lost_start = now;
                }
                *lost = new_command.value;
                if (new_command.value) {
                    *connected = false;
                }
                if (_current_mode != RgbLedMode::ParkingDistance) {
                    setCurrentMode(RgbLedMode::SystemStatus);
                    setEffectFinished(true);
                }
                break;
            }

            case RgbLedAction::SetLowBattery:
                if (new_command.side == RgbSensorSide::Left) {
                    _runtime_status.left_low_battery = new_command.value;
                } else {
                    _runtime_status.right_low_battery = new_command.value;
                }
                if (_current_mode != RgbLedMode::ParkingDistance) {
                    setCurrentMode(RgbLedMode::SystemStatus);
                    setEffectFinished(true);
                }
                break;

            case RgbLedAction::SetInsideLowBattery:
                _runtime_status.inside_low_battery = new_command.value;
                if (_current_mode != RgbLedMode::ParkingDistance) {
                    setCurrentMode(RgbLedMode::SystemStatus);
                    setEffectFinished(true);
                }
                break;

            case RgbLedAction::SetFault: {
                bool* fault = new_command.side == RgbSensorSide::Left
                    ? &_runtime_status.left_fault
                    : &_runtime_status.right_fault;
                uint32_t* fault_start = new_command.side == RgbSensorSide::Left
                    ? &_runtime_status.left_fault_flash_start_ms
                    : &_runtime_status.right_fault_flash_start_ms;
                if (*fault != new_command.value && new_command.value) {
                    *fault_start = now;
                }
                *fault = new_command.value;
                if (_current_mode != RgbLedMode::ParkingDistance) {
                    setCurrentMode(RgbLedMode::SystemStatus);
                    setEffectFinished(true);
                }
                break;
            }

            case RgbLedAction::UpdateParkingDistance:
                _runtime_status.parking_active = true;
                _runtime_status.parking_distance_cm = new_command.distance_cm;
                setCurrentMode(RgbLedMode::ParkingDistance);
                setEffectFinished(true);
                break;

            case RgbLedAction::ClearParkingDistance:
                _runtime_status.parking_active = false;
                _runtime_status.parking_distance_cm = UINT16_MAX;
                setCurrentMode(RgbLedMode::SystemStatus);
                setEffectFinished(true);
                break;
            }
        }

        const uint32_t now = millis();
        if (now - _last_render_ms < RGB_LED_FRAME_INTERVAL_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        _last_render_ms = now;

        const RgbBrightnessLevel level = _current_command.brightness == RgbBrightnessLevel::Default
            ? currentBrightness()
            : _current_command.brightness;
        _strip.setBrightness(brightnessValue(level));

        RgbColor pixels[RGB_LED_COUNT];
        clearPixels(pixels);

        const uint32_t elapsed = now - _effect_start_ms;
        RgbColor color = _current_command.color;

        switch (_current_mode) {
        case RgbLedMode::Off:
            break;

        case RgbLedMode::Solid:
            fillPixels(pixels, color);
            break;

        case RgbLedMode::Blink:
            if (blinkOn(elapsed, _current_command.period_ms)) {
                fillPixels(pixels, color);
            }
            break;

        case RgbLedMode::Breathe:
            fillPixels(pixels, scaleColor(color, triangleWave(elapsed, _current_command.period_ms)));
            break;

        case RgbLedMode::Startup: {
            const uint8_t order[] = {
                RGB_LED_INDEX_BOTTOM,
                RGB_LED_INDEX_LEFT,
                RGB_LED_INDEX_TOP,
                RGB_LED_INDEX_RIGHT
            };
            if (elapsed < STARTUP_SWEEP_MS) {
                const uint32_t step_ms = STARTUP_SWEEP_MS / RGB_LED_COUNT;
                uint32_t lit_count_value = (elapsed / step_ms) + 1;
                if (lit_count_value > RGB_LED_COUNT) {
                    lit_count_value = RGB_LED_COUNT;
                }
                const uint8_t lit_count = static_cast<uint8_t>(lit_count_value);
                for (uint8_t i = 0; i < lit_count; i++) {
                    pixels[order[i]] = RGB_COLOR_GREEN;
                }
            } else if (elapsed < STARTUP_SWEEP_MS + STARTUP_FINAL_BLINK_MS) {
                if ((elapsed - STARTUP_SWEEP_MS) < (STARTUP_FINAL_BLINK_MS / 2)) {
                    fillPixels(pixels, RGB_COLOR_GREEN);
                }
            } else {
                setCurrentMode(RgbLedMode::SystemStatus);
                setEffectFinished(true);
            }
            break;
        }

        case RgbLedMode::Standby: {
            const uint8_t order[] = {
                RGB_LED_INDEX_RIGHT,
                RGB_LED_INDEX_TOP,
                RGB_LED_INDEX_LEFT,
                RGB_LED_INDEX_BOTTOM
            };
            if (elapsed < STANDBY_FADE_MS) {
                fillPixels(pixels, RGB_COLOR_SOFT_AMBER);
                const uint32_t step_ms = STANDBY_FADE_MS / RGB_LED_COUNT;
                for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
                    const uint32_t start_ms = step_ms * i;
                    if (elapsed >= start_ms + step_ms) {
                        pixels[order[i]] = RGB_COLOR_BLACK;
                    } else if (elapsed >= start_ms) {
                        const uint8_t fade = static_cast<uint8_t>(
                            255 - (((elapsed - start_ms) * 255) / step_ms));
                        pixels[order[i]] = scaleColor(RGB_COLOR_SOFT_AMBER, fade);
                    }
                }
            } else {
                setEffectFinished(true);
            }
            break;
        }

        case RgbLedMode::UnpairedWarning:
            if (elapsed < UNPAIRED_BLINK_PERIOD_MS * 2) {
                if (blinkOn(elapsed, UNPAIRED_BLINK_PERIOD_MS)) {
                    fillPixels(pixels, RGB_COLOR_GREEN);
                }
            } else {
                setEffectFinished(true);
            }
            break;

        case RgbLedMode::Pairing:
            if (blinkOn(elapsed, PAIRING_BLINK_PERIOD_MS)) {
                fillPixels(pixels, RGB_COLOR_GREEN);
            }
            break;

        case RgbLedMode::PairSuccess:
        case RgbLedMode::FactoryReset:
            if (elapsed < PAIR_FAST_BLINK_PERIOD_MS * 3) {
                if (blinkOn(elapsed, PAIR_FAST_BLINK_PERIOD_MS)) {
                    fillPixels(pixels, RGB_COLOR_GREEN);
                }
            } else {
                fillPixels(pixels, RGB_COLOR_GREEN);
                setEffectFinished(true);
            }
            break;

        case RgbLedMode::SystemStatus: {
            const uint32_t left_fault_flash_elapsed = _runtime_status.left_fault_flash_start_ms == 0
                ? UINT32_MAX
                : now - _runtime_status.left_fault_flash_start_ms;
            const uint32_t right_fault_flash_elapsed = _runtime_status.right_fault_flash_start_ms == 0
                ? UINT32_MAX
                : now - _runtime_status.right_fault_flash_start_ms;
            const bool fault_flash_active =
                (_runtime_status.left_fault && left_fault_flash_elapsed < FAULT_FLASH_TOTAL_MS) ||
                (_runtime_status.right_fault && right_fault_flash_elapsed < FAULT_FLASH_TOTAL_MS);

            if (fault_flash_active) {
                const uint32_t fault_elapsed = min(left_fault_flash_elapsed, right_fault_flash_elapsed);
                if (blinkOn(fault_elapsed, FAULT_FLASH_PERIOD_MS)) {
                    fillPixels(pixels, RGB_COLOR_SOFT_AMBER);
                } else {
                    clearPixels(pixels);
                }
                break;
            }

            const bool any_lost = _runtime_status.left_lost || _runtime_status.right_lost;
            const bool any_low_battery =
                _runtime_status.left_low_battery ||
                _runtime_status.right_low_battery ||
                _runtime_status.inside_low_battery;
            const bool any_fault = _runtime_status.left_fault || _runtime_status.right_fault;
            const bool connected_normal = !any_lost && !any_low_battery && !any_fault;

            if (connected_normal) {
                pixels[RGB_LED_INDEX_TOP] = RGB_COLOR_GREEN;
                pixels[RGB_LED_INDEX_BOTTOM] = RGB_COLOR_GREEN;
            }

            if (_runtime_status.inside_low_battery) {
                pixels[RGB_LED_INDEX_TOP] = blinkOn(now, LOW_BATTERY_PERIOD_MS)
                    ? RGB_COLOR_SOFT_AMBER
                    : RGB_COLOR_BLACK;
            }

            if (connected_normal &&
                _runtime_status.left_connected &&
                !_runtime_status.left_lost &&
                !_runtime_status.left_low_battery &&
                !_runtime_status.left_fault) {
                pixels[RGB_LED_INDEX_LEFT] = RGB_COLOR_GREEN;
            }
            if (connected_normal &&
                _runtime_status.right_connected &&
                !_runtime_status.right_lost &&
                !_runtime_status.right_low_battery &&
                !_runtime_status.right_fault) {
                pixels[RGB_LED_INDEX_RIGHT] = RGB_COLOR_GREEN;
            }

            const bool both_lost = _runtime_status.left_lost && _runtime_status.right_lost;
            if (both_lost) {
                const uint32_t left_elapsed = now - _runtime_status.left_lost_start_ms;
                const uint32_t right_elapsed = now - _runtime_status.right_lost_start_ms;
                const bool initial = left_elapsed < LOST_INITIAL_TOTAL_MS ||
                                     right_elapsed < LOST_INITIAL_TOTAL_MS;
                const uint16_t period = initial ? LOST_INITIAL_PERIOD_MS : LOST_SLOW_PERIOD_MS;
                const uint32_t phase = now % period;
                if (phase < period / 2) {
                    pixels[RGB_LED_INDEX_LEFT] = RGB_COLOR_SOFT_AMBER;
                    pixels[RGB_LED_INDEX_RIGHT] = RGB_COLOR_BLACK;
                } else {
                    pixels[RGB_LED_INDEX_LEFT] = RGB_COLOR_BLACK;
                    pixels[RGB_LED_INDEX_RIGHT] = RGB_COLOR_SOFT_AMBER;
                }
            } else {
                if (_runtime_status.left_lost) {
                    const uint32_t lost_elapsed = now - _runtime_status.left_lost_start_ms;
                    const uint16_t period = lost_elapsed < LOST_INITIAL_TOTAL_MS
                        ? LOST_INITIAL_PERIOD_MS
                        : LOST_SLOW_PERIOD_MS;
                    pixels[RGB_LED_INDEX_LEFT] = blinkOn(lost_elapsed, period)
                        ? RGB_COLOR_SOFT_AMBER
                        : RGB_COLOR_BLACK;
                }
                if (_runtime_status.right_lost) {
                    const uint32_t lost_elapsed = now - _runtime_status.right_lost_start_ms;
                    const uint16_t period = lost_elapsed < LOST_INITIAL_TOTAL_MS
                        ? LOST_INITIAL_PERIOD_MS
                        : LOST_SLOW_PERIOD_MS;
                    pixels[RGB_LED_INDEX_RIGHT] = blinkOn(lost_elapsed, period)
                        ? RGB_COLOR_SOFT_AMBER
                        : RGB_COLOR_BLACK;
                }
            }

            if (_runtime_status.left_low_battery && !_runtime_status.left_lost) {
                pixels[RGB_LED_INDEX_LEFT] = blinkOn(now, LOW_BATTERY_PERIOD_MS)
                    ? RGB_COLOR_SOFT_AMBER
                    : RGB_COLOR_BLACK;
            }
            if (_runtime_status.right_low_battery && !_runtime_status.right_lost) {
                pixels[RGB_LED_INDEX_RIGHT] = blinkOn(now, LOW_BATTERY_PERIOD_MS)
                    ? RGB_COLOR_SOFT_AMBER
                    : RGB_COLOR_BLACK;
            }

            if (_runtime_status.left_connected_pulse_start_ms != 0 &&
                now - _runtime_status.left_connected_pulse_start_ms < CONNECTED_PULSE_TOTAL_MS &&
                connected_normal &&
                !_runtime_status.left_lost && !_runtime_status.left_fault) {
                const uint32_t pulse_elapsed = now - _runtime_status.left_connected_pulse_start_ms;
                pixels[RGB_LED_INDEX_LEFT] = (pulse_elapsed % CONNECTED_PULSE_PERIOD_MS) < CONNECTED_PULSE_ON_MS
                    ? RGB_COLOR_GREEN
                    : RGB_COLOR_BLACK;
            }
            if (_runtime_status.right_connected_pulse_start_ms != 0 &&
                now - _runtime_status.right_connected_pulse_start_ms < CONNECTED_PULSE_TOTAL_MS &&
                connected_normal &&
                !_runtime_status.right_lost && !_runtime_status.right_fault) {
                const uint32_t pulse_elapsed = now - _runtime_status.right_connected_pulse_start_ms;
                pixels[RGB_LED_INDEX_RIGHT] = (pulse_elapsed % CONNECTED_PULSE_PERIOD_MS) < CONNECTED_PULSE_ON_MS
                    ? RGB_COLOR_GREEN
                    : RGB_COLOR_BLACK;
            }

            if (_runtime_status.left_fault) {
                pixels[RGB_LED_INDEX_LEFT] = RGB_COLOR_DARK_AMBER;
            }
            if (_runtime_status.right_fault) {
                pixels[RGB_LED_INDEX_RIGHT] = RGB_COLOR_DARK_AMBER;
            }
            break;
        }

        case RgbLedMode::ParkingDistance: {
            const uint16_t dist = _runtime_status.parking_distance_cm;
            RgbColor parking_color = RGB_COLOR_GREEN;
            uint16_t period = 0;

            if (dist == UINT16_MAX || dist > 150) {
                parking_color = RGB_COLOR_GREEN;
                period = 0;
            } else if (dist > 120) {
                parking_color = RGB_COLOR_GREEN;
                period = 1200;
            } else if (dist > 90) {
                parking_color = RGB_COLOR_YELLOW;
                period = 800;
            } else if (dist > 60) {
                parking_color = RGB_COLOR_ORANGE;
                period = 500;
            } else if (dist > 30) {
                parking_color = RGB_COLOR_RED;
                period = 250;
            } else {
                parking_color = RGB_COLOR_RED;
                period = 0;
            }

            if (period == 0 || blinkOn(now, period)) {
                fillPixels(pixels, parking_color);
            }
            if (_runtime_status.left_lost) {
                const uint32_t lost_elapsed = now - _runtime_status.left_lost_start_ms;
                const uint16_t lost_period = lost_elapsed < LOST_INITIAL_TOTAL_MS
                    ? LOST_INITIAL_PERIOD_MS
                    : LOST_SLOW_PERIOD_MS;
                pixels[RGB_LED_INDEX_LEFT] = blinkOn(lost_elapsed, lost_period)
                    ? RGB_COLOR_SOFT_AMBER
                    : RGB_COLOR_BLACK;
            }
            if (_runtime_status.right_lost) {
                const uint32_t lost_elapsed = now - _runtime_status.right_lost_start_ms;
                const uint16_t lost_period = lost_elapsed < LOST_INITIAL_TOTAL_MS
                    ? LOST_INITIAL_PERIOD_MS
                    : LOST_SLOW_PERIOD_MS;
                pixels[RGB_LED_INDEX_RIGHT] = blinkOn(lost_elapsed, lost_period)
                    ? RGB_COLOR_SOFT_AMBER
                    : RGB_COLOR_BLACK;
            }
            break;
        }
        }

        for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
            _strip.setPixelColor(i, _strip.Color(pixels[i].r, pixels[i].g, pixels[i].b));
        }
        _strip.show();
        vTaskDelay(pdMS_TO_TICKS(RGB_LED_FRAME_INTERVAL_MS));
    }

    _strip.clear();
    _strip.show();
    setRgbPower(false);
}

bool RgbLedController::sendCommand(const RgbLedCommand& command)
{
    if (!_begun || _command_queue == nullptr) {
        return false;
    }

    if (xQueueSend(_command_queue, &command, 0) == pdPASS) {
        if (command.action == RgbLedAction::SetMode) {
            setEffectFinished(!isOneShotMode(command.mode));
        }
        return true;
    }

    RgbLedCommand dropped;
    (void)xQueueReceive(_command_queue, &dropped, 0);
    const bool sent = xQueueSend(_command_queue, &command, 0) == pdPASS;
    if (sent && command.action == RgbLedAction::SetMode) {
        setEffectFinished(!isOneShotMode(command.mode));
    }
    return sent;
}
