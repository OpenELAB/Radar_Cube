#include "rgb_led_controller.h"

namespace {

constexpr uint8_t RGB_LED_POWER_ENABLE_LEVEL = HIGH;
constexpr uint8_t RGB_LED_POWER_DISABLE_LEVEL = LOW;
constexpr uint32_t RGB_LED_POWER_SETTLE_DELAY_MS = 5;

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
        if (xQueueReceive(_command_queue, &new_command, 0) == pdTRUE) {
            _current_command = new_command;
            setCurrentMode(new_command.mode);
            _effect_start_ms = millis();
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

        RgbColor color = _current_command.color;

        switch (_current_command.mode) {
        case RgbLedMode::Off:
            _strip.clear();
            break;

        case RgbLedMode::Solid:
            _strip.fill(_strip.Color(color.r, color.g, color.b));
            break;

        case RgbLedMode::Blink: {
            const uint16_t period = _current_command.period_ms == 0 ? 1 : _current_command.period_ms;
            const bool on = ((now - _effect_start_ms) % period) < (period / 2);
            if (on) {
                _strip.fill(_strip.Color(color.r, color.g, color.b));
            } else {
                _strip.clear();
            }
            break;
        }

        case RgbLedMode::Breathe: {
            const uint8_t wave = triangleWave(now - _effect_start_ms, _current_command.period_ms);
            color.r = applyLevel(color.r, wave);
            color.g = applyLevel(color.g, wave);
            color.b = applyLevel(color.b, wave);
            _strip.fill(_strip.Color(color.r, color.g, color.b));
            break;
        }
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

    return xQueueOverwrite(_command_queue, &command) == pdPASS;
}
