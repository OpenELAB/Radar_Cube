#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"
#include "espnow.h"
#include "rgb_led_controller.h"
#include "speaker_controller.h"
#include "feedback_controller.h"

#ifndef LOW_BATTERY_PERCENT
#define LOW_BATTERY_PERCENT             20
#endif
#ifndef CONNECTION_LOST_TIMEOUT_MS
#define CONNECTION_LOST_TIMEOUT_MS      1500
#endif
#ifndef SENSOR_INVALID_MAX_COUNT
#define SENSOR_INVALID_MAX_COUNT        5
#endif
#ifndef DISTANCE_LEVEL_CONFIRM_COUNT
#define DISTANCE_LEVEL_CONFIRM_COUNT    2
#endif
#ifndef DISTANCE_LEVEL_MIN_SWITCH_MS
#define DISTANCE_LEVEL_MIN_SWITCH_MS    350
#endif
struct SensorLinkState {
    bool woke = false;
    bool lost_announced = false;
    uint32_t last_data_ms = 0;
    uint8_t invalid_count = 0;
};


// 系统模式
enum SysMode {
    UNPAIRED_MODE,      // 未配对 → 直接睡眠
    PAIRED_MODE,        // 配对模式
    WORK_MODE,          // 工作模式（Normal）
    TEST_MODE,          // 测试模式（预留）
    CONFIG_MODE,        // 配置模式（预留）
    FACTORY_RESET_MODE  // 恢复出厂设置
};

// ======================== 全局实例 ========================

// 串口实例（pins.h 里 extern 声明，这里定义）
HardwareSerial& LoraSerial = Serial1;
PowerManager Power;
LoraManager Lora;
EspNowManager Espnow;
MacMatch Matcher(Espnow);
SpeakerController Speaker;
RgbLedController RgbLed;
FeedbackController Feedback(RgbLed, Speaker);
static bool feedback_ready = false;

struct PairFeedbackContext {
    bool left_paired = false;
    bool right_paired = false;
};

static FeedbackScene modeToFeedbackScene(SysMode mode)
{
    switch (mode) {
    case UNPAIRED_MODE:
        return FeedbackScene::Unpaired;
    case PAIRED_MODE:
        return FeedbackScene::Pairing;
    case WORK_MODE:
        return FeedbackScene::Working;
    default:
        return FeedbackScene::Idle;
    }
}

static FeedbackSensorSet toFeedbackSensorSet(bool left_active, bool right_active)
{
    if (left_active && right_active) {
        return FeedbackSensorSet::Both;
    }
    if (left_active) {
        return FeedbackSensorSet::Left;
    }
    if (right_active) {
        return FeedbackSensorSet::Right;
    }
    return FeedbackSensorSet::None;
}

static FeedbackDistanceLevel distanceToFeedbackLevel(uint16_t distance_cm)
{
    if (distance_cm == UINT16_MAX || distance_cm > DIST_FAR_CM) {
        return FeedbackDistanceLevel::Safe;
    }
    if (distance_cm > (DIST_VERY_FAR_CM)) {
        return FeedbackDistanceLevel::VeryFar;
    }
    if (distance_cm > DIST_MID_CM) {
        return FeedbackDistanceLevel::Far;
    }
    if (distance_cm > DIST_CLOSE_CM) {
        return FeedbackDistanceLevel::Medium;
    }
    if (distance_cm > DIST_DANGER_CM) {
        return FeedbackDistanceLevel::Near;
    }
    return FeedbackDistanceLevel::Danger;
}

static void waitFeedbackOrTimeout(uint32_t timeout_ms)
{
    if (!feedback_ready) {
        return;
    }

    const uint32_t started_ms = millis();
    while (Feedback.isBusy() && millis() - started_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void pairFeedbackCallback(uint8_t slave_id,
                                 const uint8_t mac[6],
                                 void* context)
{
    (void)mac;
    auto* pair_context = static_cast<PairFeedbackContext*>(context);

    if (slave_id == SLAVE_A_ID) {
        pair_context->left_paired = true;
        if (!pair_context->right_paired && feedback_ready) {
            Feedback.onPairLeftSucceededEvent();
        }
    } else if (slave_id == SLAVE_B_ID) {
        pair_context->right_paired = true;
        if (!pair_context->left_paired && feedback_ready) {
            Feedback.onPairRightSucceededEvent();
        }
    }
}

static void enterDeepSleepWithFeedback(FeedbackScene current_scene)
{
    if (feedback_ready) {
        Feedback.onShutdownEvent(current_scene);
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
        Feedback.end();
        feedback_ready = false;
    }

    if (Speaker.isBegun()) {
        Speaker.end();
    }
    if (RgbLed.isBegun()) {
        RgbLed.end();
    }

    Power.deep_sleep();
}


// ======================== 辅助函数 ========================

// 发送结束帧（重发 END_SEND_COUNT 次确保对方收到）
static void sendEndFrame(const uint8_t* peer_mac, uint8_t head)
{
    protocol_frame_t frame;
    frame_build(&frame, head, FRAME_END);
    for (int i = 0; i < END_SEND_COUNT; i++) {
        Espnow.send(peer_mac, (uint8_t*)&frame, sizeof(frame));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// 检查是否收到结束帧
static bool checkEndFrame(uint8_t expect_head)
{
    espnow_msg_t msg;
    if (Espnow.read(&msg)) {
        if (frame_validate(msg.data, msg.len, expect_head, FRAME_END)) {
            return true;
        }
    }
    return false;
}

// 等待按键释放并返回持续时间（ms）
static uint32_t waitButtonRelease()
{
    uint32_t t_start = millis();
    // Led.led_on();
    bool led_flag = true;

    while (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
           digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED)
    {
        vTaskDelay(pdMS_TO_TICKS(300));
        if (millis() - t_start > BUTTON_LONG_PRESS_MS && led_flag) {
            // Led.led_off();
            led_flag = false;
        }
    }

    uint32_t hold_time = millis() - t_start;
    ESP_LOGI(MAIN_TAG, "Button hold: %d ms", hold_time);
    // Led.led_off();
    return hold_time;
}

// 两键同时按下从机实现比较困难，我把config_mode和factor_reset_mode对换了

// 按键 → 模式判断（USER 和 DEV 分开处理）
//   USER button: 短按 → WORK_MODE,  长按(3s) → PAIRED_MODE
//   DEV  button: 短按 → TEST_MODE,  长按(3s) → CONFIG_MODE
//   两键同按:     → FACTORY_RESET_MODE
static SysMode detectButtonMode(WakeupSource wakeup)
{
    uint32_t hold = waitButtonRelease();

    if (wakeup == WAKEUP_BOTH_BUTTONS) {
        return CONFIG_MODE;
    }

    if (wakeup == WAKEUP_USER_BUTTON) {
        return (hold > BUTTON_LONG_PRESS_MS) ? PAIRED_MODE : WORK_MODE;
    }

    if (wakeup == WAKEUP_DEV_BUTTON) {
        return (hold > BUTTON_LONG_PRESS_MS) ?  FACTORY_RESET_MODE: TEST_MODE;
    }

    return UNPAIRED_MODE;
}

// ======================== INSIDE 工作模式 ========================
static bool isRadarDistanceValid(uint16_t dist)
{
    return dist > 0 && dist <= DIST_MAX_CM;
}

static bool updateSensorDataState(SensorLinkState& sensor,
                                  const protocol_frame_t& data)
{
    const bool valid_distance = isRadarDistanceValid(data.dist);
    sensor.last_data_ms = millis();

    bool just_restored = false;
    if (valid_distance && sensor.lost_announced) {
        sensor.lost_announced = false;
        just_restored = true;
    }

    if (valid_distance) {
        sensor.invalid_count = 0;
        return just_restored;
    }

    if (sensor.invalid_count < UINT8_MAX) {
        sensor.invalid_count++;
    }
    return false;
}

static bool updateSensorConnectionLost(SensorLinkState& sensor)
{
    if (!sensor.woke || sensor.lost_announced || sensor.last_data_ms == 0) {
        return false;
    }

    if (millis() - sensor.last_data_ms >= CONNECTION_LOST_TIMEOUT_MS) {
        sensor.lost_announced = true;
        sensor.invalid_count = 0;
        return true;
    }
    return false;
}

static void inside_work_mode(uint8_t* a_mac, uint8_t* b_mac)
{
    // 1) Lora 发送唤醒帧，等从机 ESP-NOW 回复 WAKE_ACK
    bool slave_a_woke = false;
    bool slave_b_wake = false;
    bool woke = false;
    espnow_msg_t msg;
    SensorLinkState sensor_a;
    SensorLinkState sensor_b;

    // ================ 把循环里的电平切换放到外面来，加上100ms延时，之前的10ms延时不够会导致帧发送不完全 =================
    Lora.enable_ce();
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int retry = 0; retry < WAKE_MAX_RETRY && !woke; retry++) {
        Lora.sendWakeFrame();
        ESP_LOGI(MAIN_TAG, "Wake attempt %d/%d", retry + 1, WAKE_MAX_RETRY);

        for (int t = 0; t < WAKE_POLL_ROUNDS && !woke; t++) {
            vTaskDelay(pdMS_TO_TICKS(WAKE_POLL_INTERVAL_MS));
            while (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
                    // 这里判断一下是哪个从机返回的消息
                    if(memcmp(msg.src_mac, a_mac, 6) == 0) {
                        if (!slave_a_woke) {
                            if (feedback_ready) {
                                Feedback.onWakeLeftSucceededEvent();
                            }
                        }
                        slave_a_woke = true;
                        sensor_a.woke = true;
                        ESP_LOGI(MAIN_TAG, "Slave A woke up");
                    }
                    else if(memcmp(msg.src_mac, b_mac, 6) == 0) {
                        if (!slave_b_wake) {
                            if (feedback_ready) {
                                Feedback.onWakeRightSucceededEvent();
                            }
                        }
                        slave_b_wake = true;
                        sensor_b.woke = true;
                        ESP_LOGI(MAIN_TAG, "Slave B woke up");
                    }
                    if (slave_a_woke && slave_b_wake) {
                        woke = true;
                        break;
                    }
                }
            }
        }
    }
    Lora.disable_ce();

    // 唤醒失败处理
    // TODO：唤醒失败，可能是两个从机中其中一个醒来另一个出问题，还是要发送一下工作结束帧让醒来的从机停止工作
    if (!woke) {
        ESP_LOGE(MAIN_TAG, "Failed to wake slave after %d retries", WAKE_MAX_RETRY);
        if (feedback_ready) {
            Feedback.onWakeTimedOutEvent(
                toFeedbackSensorSet(slave_a_woke, slave_b_wake));
        }
        

        // 唤醒失败发送停止工作帧，确保从机全部关闭
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    

    // 唤醒成功处理：给用户明确的成功反馈
    // 播放一段上升音调，向用户发出 "Active Positive" 信号，代表雷达已启动


    // 唤醒成功后可以关 Lora 省电
    Lora.shutdown();

    // 2) 创建蜂鸣器任务
    // 初始化互斥锁
 

    // 3) 工作循环：读最新雷达数据 → 蜂鸣器提示
    uint32_t work_start = millis();

    protocol_frame_t slave_a_data = {0};
    protocol_frame_t slave_b_data  = {0};
    bool exit_flag = false;
    bool distance_feedback_started = false;
    FeedbackSensorSet last_active_sensors = FeedbackSensorSet::None;
    FeedbackDistanceLevel last_distance_level = FeedbackDistanceLevel::Safe;
    FeedbackDistanceLevel pending_distance_level = FeedbackDistanceLevel::Safe;
    uint8_t pending_distance_level_count = 0;
    uint32_t last_distance_level_switch_ms = 0;

    // 迟滞区间，用来记录上一次是什么模式，防止频繁切换

    while (true) {
        // 退出条件1：超时
        if (millis() - work_start > WORK_TIMEOUT_MS) {
            ESP_LOGI(MAIN_TAG, "Work timeout, exiting");
            break;
        }

        // 退出条件2：按键按下
        // TODO：改成中断处理而不是轮询检测？现在这样是为了在工作循环里顺便处理按键事件，感觉也还好，暂时先这样
        if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
            digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED) {
            ESP_LOGI(MAIN_TAG, "Button pressed, exiting work mode");
            break;
        }

        // 退出条件3：收到结束帧
        // (OUTSIDE 主动退出时会发 END 帧)

        // 查看一下队列里的数据深度
        // ESP_LOGI(MAIN_TAG, "Queue depth: %d", Espnow.getQueueCount());

        // TODO：队列里读取雷达数据, 如果队列里一直有数据，会不会出现卡死的情况?
        bool left_just_restored = false;
        bool right_just_restored = false;

        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // 判断哪个从机返回的数据
                if(memcmp(msg.src_mac, a_mac, 6) == 0)
                {
                    memcpy(&slave_a_data, msg.data, sizeof(protocol_frame_t));
                    left_just_restored |=
                        updateSensorDataState(sensor_a, slave_a_data);
                    ESP_LOGI(SLAVE_A_TAG, "slave_A: dist=%d mm, angle=%.2f deg"
                             , slave_a_data.dist, slave_a_data.angle * 0.01f);
                }
                else if(memcmp(msg.src_mac, b_mac, 6) == 0)
                {
                    memcpy(&slave_b_data, msg.data, sizeof(protocol_frame_t));
                    right_just_restored |=
                        updateSensorDataState(sensor_b, slave_b_data);
                    ESP_LOGI(SLAVE_B_TAG, "slave_B: dist=%d mm, angle=%.2f deg"
                             , slave_b_data.dist, slave_b_data.angle * 0.01f);
                }
            }
            else if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_END))
            {
                ESP_LOGI(MAIN_TAG, "Received END frame from slave");
                exit_flag = true;
                break;
            }
        }

        // 收到退出帧后，跳出循环
        if(exit_flag)
        {
            break;
        }

        // 模式判断
        // TODO：如果从机数据很久没有更新了，是不是也应该认为它失联了？
        const bool left_just_lost = updateSensorConnectionLost(sensor_a);
        const bool right_just_lost = updateSensorConnectionLost(sensor_b);
        const bool both_sensor_lost =
            sensor_a.lost_announced && sensor_b.lost_announced;

        if (both_sensor_lost && (left_just_lost || right_just_lost)) {
            if (feedback_ready) {
                Feedback.onBothLinksLostEvent();
            }
            exit_flag = true;
        } else if (feedback_ready) {
            if (left_just_lost) {
                Feedback.onLeftLinkLostEvent();
            }
            if (right_just_lost) {
                Feedback.onRightLinkLostEvent();
            }
            if (left_just_restored) {
                Feedback.onLeftLinkRestoredEvent();
            }
            if (right_just_restored) {
                Feedback.onRightLinkRestoredEvent();
            }
        }

        if (exit_flag) {
            break;
        }

        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // 判断数据是否有效
        // TODO：需不需要加上最大值限制？
        bool valid_a = sensor_a.woke &&
                       !sensor_a.lost_announced &&
                       isRadarDistanceValid(dist_a);
        bool valid_b = sensor_b.woke &&
                       !sensor_b.lost_announced &&
                       isRadarDistanceValid(dist_b);

        // 取判断值
        uint16_t min_dist = UINT16_MAX;
        // BuzzerMode new_mode;

        if(valid_a && valid_b)
        {
            // 如果两个都有效，取更近的那个
            min_dist = (dist_a < dist_b) ? dist_a : dist_b;
        }
        else if(valid_a)
        {
            // 如果只有 A 有效，取 A
            min_dist = dist_a;
        }
        else if(valid_b)
        {
            // 如果只有 B 有效，取 B
            min_dist = dist_b;
        }
        else
        {
            // 如果两个都无效，保持初始值
        }

        // 加上迟滞区间，防止蜂鸣器模式频繁切换

        const FeedbackSensorSet active_sensors =
            toFeedbackSensorSet(valid_a, valid_b);
        FeedbackDistanceLevel distance_level =
            distanceToFeedbackLevel(min_dist);
        bool distance_level_confirmed = !distance_feedback_started;

        if (distance_feedback_started) {
            if (distance_level == last_distance_level) {
                pending_distance_level = last_distance_level;
                pending_distance_level_count = 0;
            } else {
                if (distance_level != pending_distance_level) {
                    pending_distance_level = distance_level;
                    pending_distance_level_count = 1;
                } else if (pending_distance_level_count < UINT8_MAX) {
                    pending_distance_level_count++;
                }

                distance_level_confirmed =
                    pending_distance_level_count >= DISTANCE_LEVEL_CONFIRM_COUNT &&
                    millis() - last_distance_level_switch_ms >=
                        DISTANCE_LEVEL_MIN_SWITCH_MS;
            }
        }

        const bool active_sensors_changed =
            active_sensors != last_active_sensors;
        const bool distance_feedback_changed =
            !distance_feedback_started ||
            active_sensors_changed ||
            (distance_level != last_distance_level && distance_level_confirmed);

        if (feedback_ready &&
            active_sensors != FeedbackSensorSet::None &&
            distance_feedback_changed &&
            !Feedback.isBusy()) {
            Feedback.onDistanceLevelChangedEvent(active_sensors, distance_level);
            last_active_sensors = active_sensors;
            last_distance_level = distance_level;
            pending_distance_level = distance_level;
            pending_distance_level_count = 0;
            last_distance_level_switch_ms = millis();
            distance_feedback_started = true;
        }

        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // 4) 退出：通知从机结束。距离反馈由后续 shutdown 事件统一停止。
    sendEndFrame(a_mac, MASTER_FRAME_HEAD);
    sendEndFrame(b_mac, MASTER_FRAME_HEAD);
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}

// ======================== 唤醒源 → 模式判断 ========================

static SysMode determineMode(WakeupSource wakeup, bool has_peer)
{
    SysMode mode = UNPAIRED_MODE;

    // 车内模块：只有按键唤醒和上电
    switch (wakeup) {
    case WAKEUP_USER_BUTTON:
    case WAKEUP_DEV_BUTTON:
    case WAKEUP_BOTH_BUTTONS:
        mode = detectButtonMode(wakeup);
        break;
    default:
        // 上电复位 → 保持 UNPAIRED_MODE → 直接睡
        break;
    }

    // 工作模式需要已配对
    if (mode == WORK_MODE && !has_peer) {
        ESP_LOGW(MAIN_TAG, "Not paired, cannot enter WORK_MODE");
        mode = UNPAIRED_MODE;
    }

    return mode;
}

// ======================== 模式处理 ========================

static void handleMode(SysMode mode)
{
    switch (mode)
    {
    case FACTORY_RESET_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: FACTORY_RESET");
        // 第一版 Feedback API 暂未定义恢复出厂反馈，只处理业务动作。
        Matcher.clear_slave_mac();
        Lora.setup();               // 需要先初始化 Lora 才能清配置
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO：unpair模式要不要直接开始配对？
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        if (feedback_ready) {
            Feedback.onUnpairedDetectedEvent();
        }
        break;

    case PAIRED_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: PAIRING");
        if (feedback_ready) {
            Feedback.onPairingStartedEvent();
        }

        PairFeedbackContext pair_context;
        pair_context.left_paired = Matcher.has_slave_a_mac();
        pair_context.right_paired = Matcher.has_slave_b_mac();

        const bool pair_ok = Matcher.pair(
            PAIR_MAX_RETRY,
            pairFeedbackCallback,
            &pair_context);

        const FeedbackSensorSet paired_sensors =
            toFeedbackSensorSet(pair_context.left_paired,
                                pair_context.right_paired);

        if (!pair_ok && paired_sensors != FeedbackSensorSet::Both) {
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
            if (feedback_ready) {
                Feedback.onPairingTimedOutEvent(paired_sensors);
            }
        } else {
            if (feedback_ready) {
                Feedback.onPairBothSucceededEvent();
            }
        }

        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
        break;
    }

    case TEST_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: TEST");
        // TODO：测试模式，用于开发和工厂测试硬件
        // Led.blink(LED_PERIOD_2);
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        // TODO：配置模式，预留（OTA 等）
        // Led.blink(LED_PERIOD_2);
        break;

    case WORK_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: WORK");

        // ---- 车内模块工作流程 ----
        // 1) Lora 初始化 + 发唤醒帧
        Lora.setup();

        // 2) ESP-NOW 初始化
        Espnow.init();
        uint8_t a_mac[6]{};
        Matcher.load_slave_mac(a_mac, SLAVE_A_ID);
        uint8_t b_mac[6]{};
        Matcher.load_slave_mac(b_mac, SLAVE_B_ID);
        Espnow.addPeer(a_mac);
        Espnow.addPeer(b_mac);
        Espnow.recvStart();

        // 3) 唤醒从机 → 工作循环
        inside_work_mode(a_mac, b_mac);

        // 4) 清理
        Espnow.recvStop();
        Espnow.deinit();
        Lora.shutdown();
        // Led.led_off();
        break;
    }
    }
}

// ======================== setup ========================

void setup()
{

    // 初始化按键引脚 & 检测唤醒原因
    Power.wakeup_gpio_init();
    Power.detectWakeupSource();
    WakeupSource wakeup = Power.getWakeupSource();

    // Led.led_init();
    Power.power_init();

    // 加上打开Lora电源，第一次上电发现不加上打开电源，功率开关的使能引脚会卡在加上上拉电阻后也是0.7V左右不是3.3V导致Lora无法工作
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);

    const bool rgb_ready = RgbLed.begin();
    if (rgb_ready) {
        RgbLed.setBrightness(RGB_LED_DEFAULT_BRIGHTNESS);
    } else {
        ESP_LOGE(MAIN_TAG, "RGB LED init failed");
    }

    const bool speaker_ready = Speaker.begin();
    if (speaker_ready) {
        Speaker.setVolume(SPEAKER_DEFAULT_VOLUME);
    } else {
        ESP_LOGE(MAIN_TAG, "Speaker init failed");
    }

    if (rgb_ready && speaker_ready) {
        feedback_ready = Feedback.begin();
        if (!feedback_ready) {
            ESP_LOGE(MAIN_TAG, "Feedback controller init failed");
        }
    }

    // 电池电量检测
    uint8_t bat = Power.get_battery_value();
    if (bat <= LOW_BATTERY_PERCENT) {
        // 电量检测和低电处理由 main/电源管理负责；这里先记录低电日志。
        ESP_LOGW(MAIN_TAG, "Low battery: %u%%", bat);
    }
    // 这个可以调高一点，bat是0的时候可能不能上电了就
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        enterDeepSleepWithFeedback(FeedbackScene::Idle);
        return;
        // TODO：是不是可以在电量过低时发个警告？比如闪灯或者蜂鸣？
    }

    // 读取已配对 MAC（如果有）
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();

    // TODO：这里是不是加个else直接进入unpair模式？

    // 判断模式 → 执行 → 睡眠
    SysMode mode = determineMode(wakeup, has_peer);
    FeedbackScene scene = modeToFeedbackScene(mode);
    if (feedback_ready) {
        Feedback.onSystemBootEvent(scene);
    }
    handleMode(mode);
    enterDeepSleepWithFeedback(scene);
}

void loop() { }
