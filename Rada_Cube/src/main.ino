#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "radar.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"
#include "espnow.h"

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
#ifdef INSIDE
    BeeperControler Beeper;
#endif
#ifdef OUTSIDE
    HardwareSerial& RadarSerial = Serial1;
    RadarModule Radar;
#endif

LEDControler Led;
PowerManager Power;
LoraManager Lora;
EspNowManager Espnow;
MacMatch Matcher(Espnow);

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
    Led.led_on();
    bool led_flag = true;

    while (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
           digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED)
    {
        vTaskDelay(pdMS_TO_TICKS(300));
        if (millis() - t_start > BUTTON_LONG_PRESS_MS && led_flag) {
            Led.led_off();
            led_flag = false;
        }
    }

    uint32_t hold_time = millis() - t_start;
    ESP_LOGI(MAIN_TAG, "Button hold: %d ms", hold_time);
    Led.led_off();
    return hold_time;
}

// 按键 → 模式判断（USER 和 DEV 分开处理）
//   USER button: 短按 → WORK_MODE,  长按(3s) → PAIRED_MODE
//   DEV  button: 短按 → TEST_MODE,  长按(3s) → CONFIG_MODE
//   两键同按:     → FACTORY_RESET_MODE
static SysMode detectButtonMode(WakeupSource wakeup)
{
    uint32_t hold = waitButtonRelease();

    if (wakeup == WAKEUP_BOTH_BUTTONS) {
        return FACTORY_RESET_MODE;
    }

    if (wakeup == WAKEUP_USER_BUTTON) {
        return (hold > BUTTON_LONG_PRESS_MS) ? PAIRED_MODE : WORK_MODE;
    }

    if (wakeup == WAKEUP_DEV_BUTTON) {
        return (hold > BUTTON_LONG_PRESS_MS) ? CONFIG_MODE : TEST_MODE;
    }

    return UNPAIRED_MODE;
}

// ======================== INSIDE 工作模式 ========================
#ifdef INSIDE
static void inside_work_mode(uint8_t* peer_mac)
{
    // 1) Lora 发送唤醒帧，等从机 ESP-NOW 回复 WAKE_ACK
    bool woke = false;
    espnow_msg_t msg;

    // ================ 把循环里的电平切换放到外面来，加上100ms延时，之前的10ms延时不够会导致帧发送不完全 =================
    digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int retry = 0; retry < WAKE_MAX_RETRY && !woke; retry++) {
        Lora.sendWakeFrame();
        ESP_LOGI(MAIN_TAG, "Wake attempt %d/%d", retry + 1, WAKE_MAX_RETRY);

        for (int t = 0; t < WAKE_POLL_ROUNDS && !woke; t++) {
            vTaskDelay(pdMS_TO_TICKS(WAKE_POLL_INTERVAL_MS));
            if (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
                    woke = true;
                }
            }
        }
    }
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);

    // 唤醒失败处理
    if (!woke) {
        ESP_LOGE(MAIN_TAG, "Failed to wake slave after %d retries", WAKE_MAX_RETRY);
        
        // 建议：唤醒失败时，播放下降音调，告诉用户“车外模块失联了，不要倒车！”
        Beeper.beeper_init();
        Beeper.play_fail_tone();
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    // 唤醒成功处理：给用户明确的成功反馈
    // 播放一段上升音调，向用户发出 "Active Positive" 信号，代表雷达已启动
    Beeper.beeper_init();
    Beeper.play_success_tone();

    // 唤醒成功后可以关 Lora 省电
    Lora.shutdown();

    // 2) 工作循环：读最新雷达数据 → 蜂鸣器提示
    uint32_t work_start = millis();

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

        // 读取雷达数据
        if (Espnow.read(&msg)) {
            if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA)) {
                const protocol_frame_t* f = (const protocol_frame_t*)msg.data;
                ESP_LOGI(MAIN_TAG, "Radar: dist=%d mm, angle=%.2f deg",
                         f->dist, f->angle * 0.01f);
                
                // 这里改了逻辑
                // 之前的逻辑：远距离-》近距离蜂鸣器鸣叫周期更快,近距离-》远距离蜂鸣器鸣叫还是按照近距离的来
                // 现在的逻辑：根据距离来选择鸣叫的周期，距离越近，蜂鸣越急促，距离变远恢复到慢速或者不鸣叫
                // 距离越近，蜂鸣越急促
                if (f->dist == 0) {
                    // Workaround：雷达偶尔会读到0，实际应该是读数失败了，这时不应该急促报警而是直接不鸣叫
                    // 或者是离得太远了，超出雷达的测距范围了，这时也是读数失败了，应该不鸣叫
                    // TODO：后续优化，只有监测到物体出现之后，距离0才考虑是真的0
                    Beeper.beep_stop();
                }
                else if (f->dist < DIST_CLOSE_CM){
                    Beeper.beep(BEEPER_PERIOD_LONG);
                }
                else if (f->dist < DIST_MID_CM){
                    Beeper.beep_stop();
                    Beeper.beep(BEEPER_PERIOD_3);
                }
                else if (f->dist < DIST_FAR_CM){
                    Beeper.beep_stop();
                    Beeper.beep(BEEPER_PERIOD_1);
                }
                else{
                    Beeper.beep_stop();
                }
            }
            else if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_END)) {
                ESP_LOGI(MAIN_TAG, "Received END frame from slave");
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }

    // 3) 退出：停蜂鸣 + 通知从机结束
    Beeper.beep_stop();
    sendEndFrame(peer_mac, MASTER_FRAME_HEAD);
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}
#endif

// ======================== OUTSIDE 工作模式 ========================
#ifdef OUTSIDE
static void outside_work_mode(uint8_t* peer_mac)
{
    // 1) 回复唤醒 ACK
    protocol_frame_t ack;
    frame_build(&ack, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK);
    if (Espnow.send(peer_mac, (uint8_t*)&ack, sizeof(ack)) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Failed to send WAKE_ACK");
    }

    // Lora 不再需要，关掉串口给雷达让路（共用 Serial1）
    LoraSerial.end();

    // 2) 初始化雷达 + 定期发送雷达数据
    Radar.init();
    uint32_t work_start = millis();

    while (true) {
        // 退出条件1：超时
        if (millis() - work_start > WORK_TIMEOUT_MS) {
            ESP_LOGI(MAIN_TAG, "Work timeout, exiting");
            break;
        }

        // 退出条件2：按键按下
        if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
            digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED) {
            ESP_LOGI(MAIN_TAG, "Button pressed, exiting work mode");
            break;
        }

        // 退出条件3：收到主机结束帧
        {
            espnow_msg_t msg;
            if (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, MASTER_FRAME_HEAD, FRAME_END)) {
                    ESP_LOGI(MAIN_TAG, "Received END frame from master");
                    break;
                }
            }
        }

        // 解析雷达 + 发送
        Radar.loop();
        RadarData rd;
        if (Radar.getData(&rd)) {
            protocol_frame_t frame;
            frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA,
                        rd.dist_mm, (int16_t)(rd.angle_deg * 100));
            Espnow.send(peer_mac, (uint8_t*)&frame, sizeof(frame));
        }

        vTaskDelay(pdMS_TO_TICKS(RADAR_SEND_INTERVAL_MS));
    }

    // 3) 退出：通知主机 + 关雷达
    sendEndFrame(peer_mac, SLAVE_FRAME_HEAD);
    Radar.shutdown();
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}
#endif

// ======================== 唤醒源 → 模式判断 ========================

static SysMode determineMode(WakeupSource wakeup, bool has_peer)
{
    SysMode mode = UNPAIRED_MODE;

#ifdef INSIDE
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
#endif

#ifdef OUTSIDE
    // 车外模块：按键唤醒、Lora 唤醒、上电
    switch (wakeup) {
    case WAKEUP_LORA:
        if (has_peer) {
            ESP_LOGI(MAIN_TAG, "Lora wakeup → WORK_MODE");
            mode = WORK_MODE;
        } else {
            ESP_LOGW(MAIN_TAG, "Lora wakeup but not paired, ignoring");
        }
        break;
    case WAKEUP_USER_BUTTON:
    case WAKEUP_DEV_BUTTON:
    case WAKEUP_BOTH_BUTTONS:
        mode = detectButtonMode(wakeup);
        break;
    default:
        // 上电复位 → 保持 UNPAIRED_MODE → 直接睡
        break;
    }
#endif

    // 工作模式需要已配对
    if (mode == WORK_MODE && !has_peer) {
        ESP_LOGW(MAIN_TAG, "Not paired, cannot enter WORK_MODE");
        mode = UNPAIRED_MODE;
    }

    return mode;
}

// ======================== 模式处理 ========================

static void handleMode(SysMode mode, uint8_t* peer_mac)
{
    switch (mode)
    {
    case FACTORY_RESET_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: FACTORY_RESET");
        for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_3);
        Matcher.clearPeerMac();
        Lora.setup();               // 需要先初始化 Lora 才能清配置
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO：unpair模式要不要直接开始配对？
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        for (int i = 0; i < 2; i++) Led.blink(LED_PERIOD_2);
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRED");
        Led.blink(LED_PERIOD_1);
        if (!Matcher.pair()) {
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
        } else {
            for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_1);  // 配对成功提示
        }
        break;

    case TEST_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: TEST");
        // TODO：测试模式，用于开发和工厂测试硬件
        Led.blink(LED_PERIOD_2);
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        // TODO：配置模式，预留（OTA 等）
        Led.blink(LED_PERIOD_2);
        break;

    case WORK_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: WORK");
        Led.led_on();

#ifdef INSIDE
        // ---- 车内模块工作流程 ----
        // 1) Lora 初始化 + 发唤醒帧
        Lora.setup();

        // 2) ESP-NOW 初始化
        Espnow.init();
        Matcher.loadPeerMac(peer_mac);
        Espnow.addPeer(peer_mac);
        Espnow.recvStart();

        // 3) 唤醒从机 → 工作循环
        inside_work_mode(peer_mac);

        // 4) 清理
        Espnow.recvStop();
        Espnow.deinit();
        // 问题：前面的inside_work_mode里面已经调用了Lora.shutdown()，这里再调用一次会不会有问题？
        Lora.shutdown();
#endif

#ifdef OUTSIDE
        // ---- 车外模块工作流程 ----
        // 1) Lora 初始化（被唤醒后需要就绪状态）
        Lora.setup();

        // 2) ESP-NOW 初始化
        Espnow.init();
        Matcher.loadPeerMac(peer_mac);
        Espnow.addPeer(peer_mac);
        Espnow.recvStart();

        // 3) 回复 ACK → 雷达采集循环
        outside_work_mode(peer_mac);

        // 4) 清理
        Espnow.recvStop();
        Espnow.deinit();
        // 问题：这个函数是不是放错地方了，应该放在lora唤醒之后
        Lora.shutdown();
#endif

        Led.led_off();
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

    Led.led_init();
    Power.power_init();

    // 加上打开Lora电源，第一次上电发现不加上打开电源，功率开关的使能引脚会卡在加上上拉电阻后也是0.7V左右不是3.3V导致Lora无法工作
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);

    // 电池电量检测
    uint8_t bat = Power.get_battery_value();
    // 这个可以调高一点，bat是0的时候可能不能上电了就
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        Power.deep_sleep();
        // TODO：是不是可以在电量过低时发个警告？比如闪灯或者蜂鸣？
    }

    // 读取已配对 MAC（如果有）
    uint8_t peer_mac[6]{};
    bool has_peer = Matcher.hasPeerMac();
    if (has_peer) {
        Matcher.loadPeerMac(peer_mac);
    }
    // TODO：这里是不是加个else直接进入unpair模式？

    // 判断模式 → 执行 → 睡眠
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode, peer_mac);
    Power.deep_sleep();
}

void loop() { }
