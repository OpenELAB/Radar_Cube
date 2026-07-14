#include "rgb_led_controller.h"

#include <new>

namespace {

constexpr uint8_t RGB_LED_POWER_ENABLE_LEVEL = HIGH;
constexpr uint8_t RGB_LED_POWER_DISABLE_LEVEL = LOW;
constexpr uint32_t RGB_LED_POWER_SETTLE_DELAY_MS = 5;
constexpr uint32_t RGB_LED_TASK_STOP_TIMEOUT_MS = 500;

// 有限动画使用固定颜色、顺序和时长，不接受业务层参数。
constexpr uint32_t RGB_LED_CHASE_DURATION_MS = 1000;
constexpr uint16_t RGB_LED_FLASH_PERIOD_MS = 180;
constexpr uint8_t RGB_LED_FLASH_COUNT = 3;
constexpr uint32_t RGB_LED_FADE_OUT_DURATION_MS = 1000;

void setRgbPower(bool enabled)
{
    pinMode(RGB_LED_PWR_PIN, OUTPUT);
    digitalWrite(
        RGB_LED_PWR_PIN,
        enabled ? RGB_LED_POWER_ENABLE_LEVEL : RGB_LED_POWER_DISABLE_LEVEL);
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

    return static_cast<uint8_t>(
        ((period_ms - phase) * 255) / (period_ms - half_period));
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

bool validZone(LedZone zone)
{
    return zone >= LED_ZONE_NONE && zone <= LED_ZONE_RIGHT;
}

bool validSpeed(RgbEffectSpeed speed)
{
    return speed >= RGB_EFFECT_SPEED_SLOW && speed <= RGB_EFFECT_SPEED_FAST;
}

bool validAnimation(RgbAnimation animation)
{
    return animation >= RGB_ANIMATION_NONE && animation <= RGB_ANIMATION_FADE_OUT;
}

uint16_t blinkPeriod(RgbEffectSpeed speed)
{
    switch (speed) {
    case RGB_EFFECT_SPEED_SLOW:
        return RGB_LED_BLINK_SLOW_PERIOD_MS;
    case RGB_EFFECT_SPEED_FAST:
        return RGB_LED_BLINK_FAST_PERIOD_MS;
    case RGB_EFFECT_SPEED_MEDIUM:
    default:
        return RGB_LED_BLINK_MEDIUM_PERIOD_MS;
    }
}

uint16_t breathePeriod(RgbEffectSpeed speed)
{
    switch (speed) {
    case RGB_EFFECT_SPEED_SLOW:
        return RGB_LED_BREATHE_SLOW_PERIOD_MS;
    case RGB_EFFECT_SPEED_FAST:
        return RGB_LED_BREATHE_FAST_PERIOD_MS;
    case RGB_EFFECT_SPEED_MEDIUM:
    default:
        return RGB_LED_BREATHE_MEDIUM_PERIOD_MS;
    }
}

bool zoneContainsPixel(LedZone zone, uint8_t pixel_index)
{
    switch (zone) {
    case LED_ZONE_NONE:
        return false;
    case LED_ZONE_ALL:
        return true;
    case LED_ZONE_ONLY_LEFT:
        return pixel_index == RGB_LED_INDEX_LEFT;
    case LED_ZONE_ONLY_RIGHT:
        return pixel_index == RGB_LED_INDEX_RIGHT;
    case LED_ZONE_ONLY_SIDES:
        return pixel_index == RGB_LED_INDEX_LEFT ||
               pixel_index == RGB_LED_INDEX_RIGHT;
    case LED_ZONE_LEFT:
        return pixel_index == RGB_LED_INDEX_LEFT ||
               pixel_index == RGB_LED_INDEX_TOP ||
               pixel_index == RGB_LED_INDEX_BOTTOM;
    case LED_ZONE_RIGHT:
        return pixel_index == RGB_LED_INDEX_RIGHT ||
               pixel_index == RGB_LED_INDEX_TOP ||
               pixel_index == RGB_LED_INDEX_BOTTOM;
    default:
        return false;
    }
}

void fillZone(RgbColor pixels[RGB_LED_COUNT],
              LedZone zone,
              const RgbColor& color)
{
    for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
        if (zoneContainsPixel(zone, i)) {
            pixels[i] = color;
        }
    }
}

} // namespace

RgbLedController::RgbLedController()
{
    _active_command.type = COMMAND_OFF;
    _active_command.brightness = RGB_LED_DEFAULT_BRIGHTNESS;
}

RgbLedController::~RgbLedController()
{
    end();
}

bool RgbLedController::begin()
{
    if (isBegun()) {
        return true;
    }

    _api_mutex = xSemaphoreCreateMutex();
    if (_api_mutex == nullptr) {
        return false;
    }

    _queue_mutex = xSemaphoreCreateMutex();
    if (_queue_mutex == nullptr) {
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    _command_queue = xQueueCreate(
        RGB_LED_COMMAND_QUEUE_LENGTH,
        sizeof(Command));
    if (_command_queue == nullptr) {
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    _task_stopped = xSemaphoreCreateBinary();
    if (_task_stopped == nullptr) {
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    _strip = new (std::nothrow) Adafruit_NeoPixel(
        RGB_LED_COUNT,
        RGB_LED_PIN,
        RGB_LED_PIXEL_TYPE);
    if (_strip == nullptr || _strip->numPixels() != RGB_LED_COUNT) {
        delete _strip;
        _strip = nullptr;
        vSemaphoreDelete(_task_stopped);
        _task_stopped = nullptr;
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    taskENTER_CRITICAL(&_state_mux);
    _active_command = Command{};
    _requested_brightness = RGB_LED_DEFAULT_BRIGHTNESS;
    _current_brightness = RGB_LED_DEFAULT_BRIGHTNESS;
    _effect_start_ms = millis();
    _animation_busy = false;
    _pending_animation = false;
    _begun = false;
    _task_running = true;
    _stopping = false;
    taskEXIT_CRITICAL(&_state_mux);

    setRgbPower(true);
    delay(RGB_LED_POWER_SETTLE_DELAY_MS);

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
        setRgbPower(false);
        delete _strip;
        _strip = nullptr;
        vSemaphoreDelete(_task_stopped);
        _task_stopped = nullptr;
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
        vSemaphoreDelete(_queue_mutex);
        _queue_mutex = nullptr;
        vSemaphoreDelete(_api_mutex);
        _api_mutex = nullptr;
        return false;
    }

    setTaskHandle(created_handle);
    taskENTER_CRITICAL(&_state_mux);
    _begun = true;
    taskEXIT_CRITICAL(&_state_mux);
    return true;
}

void RgbLedController::end()
{
    SemaphoreHandle_t api_mutex = _api_mutex;
    if (api_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(api_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    taskENTER_CRITICAL(&_state_mux);
    const bool has_task = _task_handle != nullptr;
    _begun = false;
    _stopping = true;
    _task_running = false;
    SemaphoreHandle_t task_stopped = _task_stopped;
    taskEXIT_CRITICAL(&_state_mux);

    xSemaphoreGive(api_mutex);

    bool stopped_cleanly = !has_task;
    if (has_task && task_stopped != nullptr) {
        stopped_cleanly = xSemaphoreTake(
            task_stopped,
            pdMS_TO_TICKS(RGB_LED_TASK_STOP_TIMEOUT_MS)) == pdTRUE;
    }

    if (!stopped_cleanly) {
        // 与 task 原子争用退出所有权，只有取得 handle 的一方负责删除 task。
        taskENTER_CRITICAL(&_state_mux);
        TaskHandle_t task_handle = _task_handle;
        _task_handle = nullptr;
        taskEXIT_CRITICAL(&_state_mux);
        if (task_handle != nullptr) {
            vTaskDelete(task_handle);
            setTaskRunning(false);
            setAnimationBusy(false);
            releaseStripHardware();
        } else if (task_stopped != nullptr) {
            stopped_cleanly = xSemaphoreTake(
                task_stopped,
                portMAX_DELAY) == pdTRUE;
        }
    } else {
        setTaskHandle(nullptr);
    }

    if (xSemaphoreTake(api_mutex, portMAX_DELAY) == pdTRUE) {
        if (_command_queue != nullptr) {
            vQueueDelete(_command_queue);
            _command_queue = nullptr;
        }
        if (_queue_mutex != nullptr) {
            vSemaphoreDelete(_queue_mutex);
            _queue_mutex = nullptr;
        }
        if (_task_stopped != nullptr) {
            vSemaphoreDelete(_task_stopped);
            _task_stopped = nullptr;
        }

        taskENTER_CRITICAL(&_state_mux);
        _pending_animation = false;
        _stopping = false;
        taskEXIT_CRITICAL(&_state_mux);
        xSemaphoreGive(api_mutex);
    }

    _api_mutex = nullptr;
    vSemaphoreDelete(api_mutex);
}

bool RgbLedController::setBrightness(uint8_t brightness_value)
{
    const uint8_t clamped = brightness_value > RGB_LED_MAX_BRIGHTNESS
        ? RGB_LED_MAX_BRIGHTNESS
        : brightness_value;

    Command command;
    command.type = COMMAND_SET_BRIGHTNESS;
    command.brightness = clamped;
    return sendCommand(command);
}

uint8_t RgbLedController::brightness() const
{
    taskENTER_CRITICAL(&_state_mux);
    const uint8_t value = _requested_brightness;
    taskEXIT_CRITICAL(&_state_mux);
    return value;
}

bool RgbLedController::off()
{
    Command command;
    command.type = COMMAND_OFF;
    return sendCommand(command);
}

bool RgbLedController::solid(const RgbColor& color, LedZone zone)
{
    if (!validZone(zone)) {
        return false;
    }

    Command command;
    command.type = COMMAND_SOLID;
    command.color = color;
    command.zone = zone;
    return sendCommand(command);
}

bool RgbLedController::blink(const RgbColor& color,
                             RgbEffectSpeed speed,
                             LedZone zone)
{
    if (!validSpeed(speed) || !validZone(zone)) {
        return false;
    }

    Command command;
    command.type = COMMAND_BLINK;
    command.color = color;
    command.speed = speed;
    command.zone = zone;
    return sendCommand(command);
}

bool RgbLedController::breathe(const RgbColor& color,
                               RgbEffectSpeed speed,
                               LedZone zone)
{
    if (!validSpeed(speed) || !validZone(zone)) {
        return false;
    }

    Command command;
    command.type = COMMAND_BREATHE;
    command.color = color;
    command.speed = speed;
    command.zone = zone;
    return sendCommand(command);
}

bool RgbLedController::playAnimation(RgbAnimation animation)
{
    if (!validAnimation(animation)) {
        return false;
    }
    if (animation == RGB_ANIMATION_NONE) {
        return off();
    }

    Command command;
    command.type = COMMAND_ANIMATION;
    command.animation = animation;
    return sendCommand(command);
}

bool RgbLedController::isBusy() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool busy = _animation_busy || _pending_animation;
    taskEXIT_CRITICAL(&_state_mux);
    return busy;
}

bool RgbLedController::isBegun() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool begun = _begun;
    taskEXIT_CRITICAL(&_state_mux);
    return begun;
}

bool RgbLedController::isTaskRunning() const
{
    taskENTER_CRITICAL(&_state_mux);
    const bool running = _task_running && _task_handle != nullptr;
    taskEXIT_CRITICAL(&_state_mux);
    return running;
}

void RgbLedController::taskEntry(void* arg)
{
    RgbLedController* controller = static_cast<RgbLedController*>(arg);
    if (controller == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    controller->taskLoop();
}

void RgbLedController::taskLoop()
{
    _strip->begin();
    _strip->setBrightness(_current_brightness);
    _strip->clear();
    _strip->show();

    while (taskShouldRun()) {
        Command command;
        // task 只获取队列锁，避免强制删除时遗留已锁住的 API 生命周期锁。
        SemaphoreHandle_t queue_mutex = _queue_mutex;
        if (queue_mutex != nullptr &&
            xSemaphoreTake(queue_mutex, portMAX_DELAY) == pdTRUE) {
            if (_command_queue != nullptr &&
                xQueueReceive(_command_queue, &command, 0) == pdTRUE) {
                handleCommand(command);
            }
            xSemaphoreGive(queue_mutex);
        }

        RgbColor pixels[RGB_LED_COUNT];
        renderFrame(millis(), pixels);
        for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
            _strip->setPixelColor(
                i,
                _strip->Color(pixels[i].r, pixels[i].g, pixels[i].b));
        }
        _strip->show();

        vTaskDelay(pdMS_TO_TICKS(RGB_LED_FRAME_INTERVAL_MS));
    }

    releaseStripHardware();

    setAnimationBusy(false);
    setTaskRunning(false);

    // task 先取得退出所有权再通知 end()；若 end() 已接管，则等待被强制删除。
    taskENTER_CRITICAL(&_state_mux);
    const bool owns_task_exit = _task_handle != nullptr;
    if (owns_task_exit) {
        _task_handle = nullptr;
    }
    taskEXIT_CRITICAL(&_state_mux);

    if (owns_task_exit) {
        SemaphoreHandle_t task_stopped = _task_stopped;
        if (task_stopped != nullptr) {
            xSemaphoreGive(task_stopped);
        }
        vTaskDelete(nullptr);
    }

    vTaskSuspend(nullptr);
}

bool RgbLedController::sendCommand(Command command)
{
    SemaphoreHandle_t api_mutex = _api_mutex;
    if (api_mutex == nullptr || xSemaphoreTake(api_mutex, 0) != pdTRUE) {
        return false;
    }

    taskENTER_CRITICAL(&_state_mux);
    const bool can_send = _begun && !_stopping && _command_queue != nullptr;
    taskEXIT_CRITICAL(&_state_mux);

    if (!can_send) {
        xSemaphoreGive(api_mutex);
        return false;
    }

    SemaphoreHandle_t queue_mutex = _queue_mutex;
    if (queue_mutex == nullptr ||
        xSemaphoreTake(queue_mutex, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(api_mutex);
        return false;
    }

    Command queued_command = command;
    if (command.type == COMMAND_SET_BRIGHTNESS) {
        // 亮度更新合并进待执行视觉命令，不能把该视觉命令覆盖掉。
        Command pending_command;
        if (xQueuePeek(_command_queue, &pending_command, 0) == pdTRUE) {
            pending_command.brightness = command.brightness;
            queued_command = pending_command;
        }
    } else {
        taskENTER_CRITICAL(&_state_mux);
        queued_command.brightness = _requested_brightness;
        taskEXIT_CRITICAL(&_state_mux);
    }

    const bool sent =
        xQueueOverwrite(_command_queue, &queued_command) == pdPASS;
    if (sent) {
        taskENTER_CRITICAL(&_state_mux);
        if (command.type == COMMAND_SET_BRIGHTNESS) {
            _requested_brightness = command.brightness;
        }
        _pending_animation = queued_command.type == COMMAND_ANIMATION &&
                             queued_command.animation != RGB_ANIMATION_NONE;
        taskEXIT_CRITICAL(&_state_mux);
    }

    xSemaphoreGive(queue_mutex);
    xSemaphoreGive(api_mutex);
    return sent;
}

void RgbLedController::handleCommand(const Command& command)
{
    taskENTER_CRITICAL(&_state_mux);
    _pending_animation = false;
    if (command.type != COMMAND_SET_BRIGHTNESS) {
        _animation_busy = command.type == COMMAND_ANIMATION &&
                          command.animation != RGB_ANIMATION_NONE;
    }
    taskEXIT_CRITICAL(&_state_mux);

    _current_brightness = command.brightness;
    _strip->setBrightness(_current_brightness);

    if (command.type == COMMAND_SET_BRIGHTNESS) {
        return;
    }

    _active_command = command;
    _effect_start_ms = millis();
}

void RgbLedController::renderFrame(uint32_t now,
                                   RgbColor pixels[RGB_LED_COUNT])
{
    clearPixels(pixels);
    const uint32_t elapsed = now - _effect_start_ms;

    switch (_active_command.type) {
    case COMMAND_OFF:
        return;

    case COMMAND_SOLID:
        fillZone(pixels, _active_command.zone, _active_command.color);
        return;

    case COMMAND_BLINK:
        if (blinkOn(elapsed, blinkPeriod(_active_command.speed))) {
            fillZone(pixels, _active_command.zone, _active_command.color);
        }
        return;

    case COMMAND_BREATHE:
        fillZone(
            pixels,
            _active_command.zone,
            scaleColor(
                _active_command.color,
                triangleWave(elapsed, breathePeriod(_active_command.speed))));
        return;

    case COMMAND_SET_BRIGHTNESS:
        return;

    case COMMAND_ANIMATION:
        break;
    }

    if (_active_command.animation == RGB_ANIMATION_CHASE_CLOCKWISE ||
        _active_command.animation == RGB_ANIMATION_CHASE_COUNTERCLOCKWISE) {
        if (elapsed >= RGB_LED_CHASE_DURATION_MS) {
            _active_command.type = COMMAND_OFF;
            setAnimationBusy(false);
            return;
        }

        const uint8_t clockwise_order[RGB_LED_COUNT] = {
            RGB_LED_INDEX_BOTTOM,
            RGB_LED_INDEX_LEFT,
            RGB_LED_INDEX_TOP,
            RGB_LED_INDEX_RIGHT
        };
        const uint8_t counterclockwise_order[RGB_LED_COUNT] = {
            RGB_LED_INDEX_BOTTOM,
            RGB_LED_INDEX_RIGHT,
            RGB_LED_INDEX_TOP,
            RGB_LED_INDEX_LEFT
        };
        const uint32_t step_ms = RGB_LED_CHASE_DURATION_MS / RGB_LED_COUNT;
        uint8_t step = static_cast<uint8_t>(elapsed / step_ms);
        if (step >= RGB_LED_COUNT) {
            step = RGB_LED_COUNT - 1;
        }
        const uint8_t* order =
            _active_command.animation == RGB_ANIMATION_CHASE_CLOCKWISE
            ? clockwise_order
            : counterclockwise_order;
        pixels[order[step]] = RGB_COLOR_GREEN;
        return;
    }

    if (_active_command.animation == RGB_ANIMATION_FLASH) {
        const uint32_t total_ms =
            static_cast<uint32_t>(RGB_LED_FLASH_PERIOD_MS) * RGB_LED_FLASH_COUNT;
        if (elapsed >= total_ms) {
            _active_command.type = COMMAND_OFF;
            setAnimationBusy(false);
            return;
        }
        if (blinkOn(elapsed, RGB_LED_FLASH_PERIOD_MS)) {
            fillPixels(pixels, RGB_COLOR_GREEN);
        }
        return;
    }

    if (_active_command.animation == RGB_ANIMATION_FADE_OUT) {
        if (elapsed >= RGB_LED_FADE_OUT_DURATION_MS) {
            _active_command.type = COMMAND_OFF;
            setAnimationBusy(false);
            return;
        }

        fillPixels(pixels, RGB_COLOR_SOFT_AMBER);
        const uint8_t order[RGB_LED_COUNT] = {
            RGB_LED_INDEX_RIGHT,
            RGB_LED_INDEX_TOP,
            RGB_LED_INDEX_LEFT,
            RGB_LED_INDEX_BOTTOM
        };
        const uint32_t step_ms = RGB_LED_FADE_OUT_DURATION_MS / RGB_LED_COUNT;
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
        return;
    }

    _active_command.type = COMMAND_OFF;
    setAnimationBusy(false);
}

void RgbLedController::setAnimationBusy(bool busy)
{
    taskENTER_CRITICAL(&_state_mux);
    _animation_busy = busy;
    taskEXIT_CRITICAL(&_state_mux);
}

void RgbLedController::setTaskHandle(TaskHandle_t handle)
{
    taskENTER_CRITICAL(&_state_mux);
    _task_handle = handle;
    taskEXIT_CRITICAL(&_state_mux);
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

void RgbLedController::releaseStripHardware()
{
    if (_strip != nullptr) {
        _strip->clear();
        _strip->show();

        // 析构函数按 Adafruit NeoPixel 的 ESP32 实现释放 RMT 和像素缓冲区。
        delete _strip;
        _strip = nullptr;
    }
    setRgbPower(false);
}
