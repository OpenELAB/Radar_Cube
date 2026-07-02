#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"
#include "espnow.h"
#include "rgb_led_controller.h"

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
static RgbLedController RgbLed;


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

void onPairingStart()
{
    RgbLed.breathe(RGB_COLOR_BLUE, 1800);
}

void onPairSuccess()
{
    RgbLed.blink(RGB_COLOR_GREEN, 250);
}

void onWakeFail()
{
    RgbLed.blink(RGB_COLOR_RED, 300);
}

static constexpr uint8_t PARKING_LED_UNKNOWN = 0;
static constexpr uint8_t PARKING_LED_FAR = 1;
static constexpr uint8_t PARKING_LED_MID_BREATHE = 2;
static constexpr uint8_t PARKING_LED_MID_BLINK = 3;
static constexpr uint8_t PARKING_LED_CLOSE = 4;
static constexpr uint8_t PARKING_LED_DANGER = 5;

static uint8_t parkingLedStateForDistance(uint16_t distance_cm)
{
    if (distance_cm > DIST_FAR_CM) {
        return PARKING_LED_FAR;
    }
    if (distance_cm > DIST_MID_CM) {
        return PARKING_LED_MID_BREATHE;
    }
    if (distance_cm > DIST_CLOSE_CM) {
        return PARKING_LED_MID_BLINK;
    }
    if (distance_cm > DIST_DANGER_CM) {
        return PARKING_LED_CLOSE;
    }
    return PARKING_LED_DANGER;
}

void updateParkingDistance(uint16_t distance_cm)
{
    static uint8_t current_state = PARKING_LED_UNKNOWN;
    const uint8_t next_state = parkingLedStateForDistance(distance_cm);

    if (next_state == current_state) {
        return;
    }

    current_state = next_state;

    switch (next_state) {
    case PARKING_LED_FAR:
        RgbLed.solid(RGB_COLOR_GREEN);
        break;
    case PARKING_LED_MID_BREATHE:
        RgbLed.breathe(RGB_COLOR_YELLOW, 1200);
        break;
    case PARKING_LED_MID_BLINK:
        RgbLed.blink(RGB_COLOR_YELLOW, 600);
        break;
    case PARKING_LED_CLOSE:
        RgbLed.blink(RGB_COLOR_ORANGE, 300);
        break;
    case PARKING_LED_DANGER:
        RgbLed.blink(RGB_COLOR_RED, 180);
        break;
    case PARKING_LED_UNKNOWN:
    default:
        break;
    }
}

void beforeDeepSleep()
{
    RgbLed.off();
    vTaskDelay(pdMS_TO_TICKS(50));
    RgbLed.end();
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
static void inside_work_mode(uint8_t* a_mac, uint8_t* b_mac)
{
    // 1) Lora 发送唤醒帧，等从机 ESP-NOW 回复 WAKE_ACK
    bool slave_a_woke = false;
    bool slave_b_wake = false;
    bool woke = false;
    espnow_msg_t msg;

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
                        slave_a_woke = true;
                        ESP_LOGI(MAIN_TAG, "Slave A woke up");
                    }
                    else if(memcmp(msg.src_mac, b_mac, 6) == 0) {
                        slave_b_wake = true;
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
        onWakeFail();
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        // 建议：唤醒失败时，播放下降音调，告诉用户“车外模块失联了，不要倒车！”
        // Beeper.beeper_init();
        // Beeper.play_fail_tone();

        // 唤醒失败发送停止工作帧，确保从机全部关闭
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    

    // 唤醒成功处理：给用户明确的成功反馈
    // 播放一段上升音调，向用户发出 "Active Positive" 信号，代表雷达已启动
    // Beeper.beeper_init();
    // Beeper.play_success_tone();

    // 唤醒成功后可以关 Lora 省电
    Lora.shutdown();

    // 2) 创建蜂鸣器任务

    // 3) 工作循环：读最新雷达数据 → 蜂鸣器提示
    uint32_t work_start = millis();

    protocol_frame_t slave_a_data = {0};
    protocol_frame_t slave_b_data  = {0};
    uint32_t slave_a_update_ms = 0;
    uint32_t slave_b_update_ms = 0;
    bool exit_flag = false;

    // 迟滞区间，用来记录上一次是什么模式，防止频繁切换
    // BuzzerMode current_mode = SILENT_MODE;

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

        // TODO：队列里读取雷达数据, 如果队列里一直有数据，会不会出现卡死的情况?
        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // 判断哪个从机返回的数据
                if(memcmp(msg.src_mac, a_mac, 6) == 0)
                {
                    memcpy(&slave_a_data, msg.data, sizeof(protocol_frame_t));
                    slave_a_update_ms = millis();
                    ESP_LOGI(SLAVE_A_TAG, "slave_A: dist=%d cm, angle=%.2f deg"
                             , slave_a_data.dist, slave_a_data.angle * 0.01f);
                }
                else if(memcmp(msg.src_mac, b_mac, 6) == 0)
                {
                    memcpy(&slave_b_data, msg.data, sizeof(protocol_frame_t));
                    slave_b_update_ms = millis();
                    ESP_LOGI(SLAVE_B_TAG, "slave_B: dist=%d cm, angle=%.2f deg"
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
        const uint32_t now_ms = millis();
        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // 判断数据是否有效
        // TODO：需不需要加上最大值限制？
        bool valid_a = (slave_a_update_ms != 0) &&
                       (now_ms - slave_a_update_ms <= DIST_DATA_TIMEOUT_MS) &&
                       (dist_a >= DIST_MIN_CM) &&
                       (dist_a <= DIST_MAX_CM);
        bool valid_b = (slave_b_update_ms != 0) &&
                       (now_ms - slave_b_update_ms <= DIST_DATA_TIMEOUT_MS) &&
                       (dist_b >= DIST_MIN_CM) &&
                       (dist_b <= DIST_MAX_CM);

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
        // TODO：看一下需不需换成滤波还是在加上一层滤波
        if (min_dist != UINT16_MAX) {
            updateParkingDistance(min_dist);
        } else {
            updateParkingDistance(DIST_MAX_CM);
        }


        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // 改为主动杀死进程
    vTaskDelay(pdMS_TO_TICKS(200));
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
        // 清除
        Matcher.clear_slave_mac();
        Lora.setup();               // 需要先初始化 Lora 才能清配置
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO：unpair模式要不要直接开始配对？
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRED");
        // TODO：这里需不要要用灯来指示配对过程，比如处于配对时用呼吸灯？
        onPairingStart();
        if (!Matcher.pair()) {
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
            onWakeFail();
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else {
            onPairSuccess();
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        break;

    case TEST_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: TEST");
        // TODO：测试模式，用于开发和工厂测试硬件
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        // TODO：配置模式，预留（OTA 等）
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

    RgbLed.begin();
    RgbLed.setBrightness(RgbBrightnessLevel::Medium);
    RgbLed.solid(RGB_COLOR_GREEN);

    // 加上打开Lora电源，第一次上电发现不加上打开电源，功率开关的使能引脚会卡在加上上拉电阻后也是0.7V左右不是3.3V导致Lora无法工作
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);

    // 电池电量检测
    uint8_t bat = Power.get_battery_value();
    // 这个可以调高一点，bat是0的时候可能不能上电了就
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        beforeDeepSleep();
        Power.deep_sleep();
        // TODO：是不是可以在电量过低时发个警告？比如闪灯或者蜂鸣？
    }

    // 读取已配对 MAC（如果有）
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();

    // TODO：这里是不是加个else直接进入unpair模式？

    // 判断模式 → 执行 → 睡眠
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    beforeDeepSleep();
    Power.deep_sleep();
}

void loop() { }
