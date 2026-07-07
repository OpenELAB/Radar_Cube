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
#include "radar_audio_manager.h"

#ifndef LOW_BATTERY_PERCENT
#define LOW_BATTERY_PERCENT             20
#endif
#ifndef CONNECTION_LOST_TIMEOUT_MS
#define CONNECTION_LOST_TIMEOUT_MS      1500
#endif
#ifndef CONNECTION_LOST_REPEAT_COUNT
#define CONNECTION_LOST_REPEAT_COUNT    3
#endif
#ifndef SENSOR_INVALID_MAX_COUNT
#define SENSOR_INVALID_MAX_COUNT        5
#endif
#ifndef SENSOR_FAULT_COOLDOWN_MS
#define SENSOR_FAULT_COOLDOWN_MS        5000
#endif
#define SENSOR_RESERVE_LOW_BATTERY_FLAG 0x01

struct SensorLinkState {
    bool woke = false;
    bool lost_announced = false;
    uint32_t last_data_ms = 0;
    uint32_t last_fault_prompt_ms = 0;
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
// RadarAudio 是上层音频业务层：main 负责判断事件，Speaker 只负责播放路径。
RadarAudioManager RadarAudio;

static void rgbEffectDrain(uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    while (RgbLed.isBegun() &&
           !RgbLed.effectFinished() &&
           millis() - start_ms < timeout_ms) {
        RadarAudio.update();
        vTaskDelay(pdMS_TO_TICKS(RGB_LED_FRAME_INTERVAL_MS));
    }
}

static void queueConnectionLostPrompt(bool left_lost, bool right_lost)
{
    if (!left_lost && !right_lost) {
        return;
    }

    const bool both_lost = left_lost && right_lost;
    // 双侧失联会让停车距离信息失效，按安全关键事件处理；单侧失联仍是工作告警。
    const RadarEventSeverity severity =
        both_lost ? RadarEventSeverity::SafetyCritical : RadarEventSeverity::WorkWarning;
    const RadarEventTarget target =
        both_lost ? RadarEventTarget::Both :
        (left_lost ? RadarEventTarget::Left : RadarEventTarget::Right);

    for (uint8_t i = 0; i < CONNECTION_LOST_REPEAT_COUNT; i++) {
        RadarAudio.queue(RadarAudioId::LinkLost, SpeakerVolumeLevel::High, severity, target);
    }
}

static void queueShutdownPrompt()
{
    RadarAudio.queue(RadarAudioId::SysShutdown,
                     SpeakerVolumeLevel::Low,
                     RadarEventSeverity::Lifecycle,
                     RadarEventTarget::Inside);
}

static void pairAudioCallback(uint8_t slave_id, const uint8_t mac[6], void* context)
{
    (void)mac;
    (void)context;

    const RadarEventTarget target =
        (slave_id == SLAVE_A_ID) ? RadarEventTarget::Left :
        (slave_id == SLAVE_B_ID) ? RadarEventTarget::Right :
        RadarEventTarget::Nearest;

    RadarAudio.queue(RadarAudioId::PairOk,
                     SpeakerVolumeLevel::High,
                     RadarEventSeverity::FlowResult,
                     target);
    RadarAudio.update();
}

static void enterDeepSleepWithAudio()
{
    if (RgbLed.isBegun()) {
        RgbLed.standby();
        rgbEffectDrain(1200);
    }

    if (Speaker.isBegun()) {
        Speaker.setKeepOutputAlive(false);
        queueShutdownPrompt();
        RadarAudio.drain();
        Speaker.stop();
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
        RadarAudio.update();
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

static void queueFaultPromptWithCooldown(SensorLinkState& sensor, RadarEventTarget target)
{
    const uint32_t now = millis();
    if (sensor.last_fault_prompt_ms == 0 ||
        now - sensor.last_fault_prompt_ms >= SENSOR_FAULT_COOLDOWN_MS) {
        RadarAudio.queue(RadarAudioId::Fault,
                         SpeakerVolumeLevel::High,
                         RadarEventSeverity::SafetyCritical,
                         target);
        sensor.last_fault_prompt_ms = now;
    }
}

static void updateSensorDataState(SensorLinkState& sensor,
                                  const protocol_frame_t& data,
                                  RadarEventTarget target)
{
    const bool valid_distance = isRadarDistanceValid(data.dist);
    sensor.last_data_ms = millis();

    if (sensor.lost_announced) {
        sensor.lost_announced = false;
    }

    if (valid_distance) {
        sensor.invalid_count = 0;
        return;
    }

    if (sensor.invalid_count < UINT8_MAX) {
        sensor.invalid_count++;
    }
    if (sensor.invalid_count >= SENSOR_INVALID_MAX_COUNT) {
        queueFaultPromptWithCooldown(sensor, target);
    }
}

static bool checkSensorConnectionLost(SensorLinkState& sensor, RgbSensorSide side)
{
    if (!sensor.woke || sensor.lost_announced || sensor.last_data_ms == 0) {
        return false;
    }

    if (millis() - sensor.last_data_ms >= CONNECTION_LOST_TIMEOUT_MS) {
        sensor.lost_announced = true;
        sensor.invalid_count = 0;
        RgbLed.setSensorLost(side, true);
        return true;
    }
    return false;
}

static void inside_work_mode(uint8_t* a_mac, uint8_t* b_mac)
{
    RgbLed.showSystemStatus();
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
        RadarAudio.update();
        Lora.sendWakeFrame();
        ESP_LOGI(MAIN_TAG, "Wake attempt %d/%d", retry + 1, WAKE_MAX_RETRY);

        for (int t = 0; t < WAKE_POLL_ROUNDS && !woke; t++) {
            RadarAudio.update();
            vTaskDelay(pdMS_TO_TICKS(WAKE_POLL_INTERVAL_MS));
            while (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
                    // 这里判断一下是哪个从机返回的消息
                    if(memcmp(msg.src_mac, a_mac, 6) == 0) {
                        if (!slave_a_woke) {
                            RadarAudio.queue(RadarAudioId::PairOk,
                                             SpeakerVolumeLevel::High,
                                             RadarEventSeverity::FlowResult,
                                             RadarEventTarget::Left);
                            RgbLed.sensorConnectedPulse(RgbSensorSide::Left);
                        }
                        slave_a_woke = true;
                        sensor_a.woke = true;
                        ESP_LOGI(MAIN_TAG, "Slave A woke up");
                    }
                    else if(memcmp(msg.src_mac, b_mac, 6) == 0) {
                        if (!slave_b_wake) {
                            RadarAudio.queue(RadarAudioId::PairOk,
                                             SpeakerVolumeLevel::High,
                                             RadarEventSeverity::FlowResult,
                                             RadarEventTarget::Right);
                            RgbLed.sensorConnectedPulse(RgbSensorSide::Right);
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
        RgbLed.setSensorLost(RgbSensorSide::Left, true);
        RgbLed.setSensorLost(RgbSensorSide::Right, true);
        queueConnectionLostPrompt(true, true);
        

        // 唤醒失败发送停止工作帧，确保从机全部关闭
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        
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
    uint16_t dist_min = 0;
    bool exit_flag = false;

    // 迟滞区间，用来记录上一次是什么模式，防止频繁切换

    while (true) {
        RadarAudio.update();

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
        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // 判断哪个从机返回的数据
                if(memcmp(msg.src_mac, a_mac, 6) == 0)
                {
                    memcpy(&slave_a_data, msg.data, sizeof(protocol_frame_t));
                    updateSensorDataState(sensor_a, slave_a_data, RadarEventTarget::Left);
                    RgbLed.setSensorLost(RgbSensorSide::Left, false);
                    RgbLed.setSensorLowBattery(
                        RgbSensorSide::Left,
                        (slave_a_data.reserve & SENSOR_RESERVE_LOW_BATTERY_FLAG) != 0);
                    RgbLed.setSensorFault(
                        RgbSensorSide::Left,
                        sensor_a.invalid_count >= SENSOR_INVALID_MAX_COUNT);
                    ESP_LOGI(SLAVE_A_TAG, "slave_A: dist=%d mm, angle=%.2f deg"
                             , slave_a_data.dist, slave_a_data.angle * 0.01f);
                }
                else if(memcmp(msg.src_mac, b_mac, 6) == 0)
                {
                    memcpy(&slave_b_data, msg.data, sizeof(protocol_frame_t));
                    updateSensorDataState(sensor_b, slave_b_data, RadarEventTarget::Right);
                    RgbLed.setSensorLost(RgbSensorSide::Right, false);
                    RgbLed.setSensorLowBattery(
                        RgbSensorSide::Right,
                        (slave_b_data.reserve & SENSOR_RESERVE_LOW_BATTERY_FLAG) != 0);
                    RgbLed.setSensorFault(
                        RgbSensorSide::Right,
                        sensor_b.invalid_count >= SENSOR_INVALID_MAX_COUNT);
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
        const bool left_lost_changed = checkSensorConnectionLost(sensor_a, RgbSensorSide::Left);
        const bool right_lost_changed = checkSensorConnectionLost(sensor_b, RgbSensorSide::Right);
        if (left_lost_changed || right_lost_changed) {
            queueConnectionLostPrompt(sensor_a.lost_announced, sensor_b.lost_announced);
        }

        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // 判断数据是否有效
        // TODO：需不需要加上最大值限制？
        bool valid_a = !sensor_a.lost_announced && isRadarDistanceValid(dist_a);
        bool valid_b = !sensor_b.lost_announced && isRadarDistanceValid(dist_b);

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

        const bool both_sensor_lost = sensor_a.lost_announced && sensor_b.lost_announced;
        const bool any_sensor_fault =
            sensor_a.invalid_count >= SENSOR_INVALID_MAX_COUNT ||
            sensor_b.invalid_count >= SENSOR_INVALID_MAX_COUNT;

        if (both_sensor_lost || any_sensor_fault || min_dist == UINT16_MAX) {
            RgbLed.clearParkingDistance();
        } else {
            RgbLed.updateParkingDistance(min_dist);
        }

        if (RadarAudio.blocksDistance()) {
            RadarAudio.stopDistance();
        } else {
            RadarAudio.updateDistance(min_dist);
        }

        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // 改为主动杀死进程
    RadarAudio.stopDistance();
    vTaskDelay(pdMS_TO_TICKS(200));

    // 4) 退出：停蜂鸣 + 通知从机结束
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
        RgbLed.factoryReset();
        rgbEffectDrain(800);
        // for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_3);
        // 清除
        Matcher.clear_slave_mac();
        Lora.setup();               // 需要先初始化 Lora 才能清配置
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO：unpair模式要不要直接开始配对？
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        RgbLed.unpairedWarning();
        rgbEffectDrain(800);
        // for (int i = 0; i < 2; i++) Led.blink(LED_PERIOD_2);
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRED");
        RgbLed.pairing();
        // TODO：这里需不要要用灯来指示配对过程，比如处于配对时用呼吸灯？
        // Led.blink(LED_PERIOD_1);
        if (!Matcher.pair(PAIR_MAX_RETRY, pairAudioCallback, nullptr)) {
            RadarAudio.queue(RadarAudioId::PairFail,
                             SpeakerVolumeLevel::High,
                             RadarEventSeverity::FlowResult,
                             RadarEventTarget::Both);
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
        } else {
            RgbLed.pairSuccess();
            rgbEffectDrain(800);
            // for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_1);  // 配对成功提示
        }
        break;

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
        Speaker.setKeepOutputAlive(true);
        // Led.led_on();

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
        Speaker.setKeepOutputAlive(false);
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

    if (RgbLed.begin()) {
        RgbLed.setBrightness(RgbBrightnessLevel::Medium);
        RgbLed.startup();
        rgbEffectDrain(1400);
    } else {
        ESP_LOGE(MAIN_TAG, "RGB LED init failed");
    }

    if (Speaker.begin()) {
        RadarAudio.begin(Speaker);
        Speaker.setVolume(SpeakerVolumeLevel::High);
        RadarAudio.queue(RadarAudioId::SysBoot,
                         SpeakerVolumeLevel::High,
                         RadarEventSeverity::Lifecycle,
                         RadarEventTarget::Inside);
        RadarAudio.update();
    } else {
        ESP_LOGE(MAIN_TAG, "Speaker init failed");
    }

    // 电池电量检测
    uint8_t bat = Power.get_battery_value();
    if (bat == 0) {
        RgbLed.setInsideLowBattery(true);
        RadarAudio.queue(RadarAudioId::PowerCritical,
                         SpeakerVolumeLevel::High,
                         RadarEventSeverity::SafetyCritical,
                         RadarEventTarget::Inside);
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        enterDeepSleepWithAudio();
        return;
    }

    if (bat <= LOW_BATTERY_PERCENT) {
        RgbLed.setInsideLowBattery(true);
        RadarAudio.queue(RadarAudioId::PowerLow,
                         SpeakerVolumeLevel::High,
                         RadarEventSeverity::WorkWarning,
                         RadarEventTarget::Inside);
    }
    // 读取已配对 MAC（如果有）
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();

    // TODO：这里是不是加个else直接进入unpair模式？

    // 判断模式 → 执行 → 睡眠
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    enterDeepSleepWithAudio();
}

void loop() { }
