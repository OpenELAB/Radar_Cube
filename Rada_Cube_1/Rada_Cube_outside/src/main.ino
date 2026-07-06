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
// #ifdef INSIDE
//     BeeperControler Beeper;
// #endif
// #ifdef OUTSIDE
    HardwareSerial& RadarSerial = Serial1;
    RadarModule Radar;
// #endif

LEDControler Led;
PowerManager Power;
LoraManager Lora;
EspNowManager Espnow;
MacMatch Matcher(Espnow);

// ======================== 辅助函数 ========================

// 发送结束帧（重发 END_SEND_COUNT 次确保对方收到）
static void sendEndFrame(const uint8_t* peer_mac, uint8_t head, uint8_t battery = BATTERY_INVALID)
{
    protocol_frame_t frame;
    frame_build(&frame, head, FRAME_END, 0, 0, battery);
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


// ======================== OUTSIDE 工作模式 ========================
// #ifdef OUTSIDE
static void outside_work_mode(uint8_t* peer_mac, uint8_t battery)
{
    // 1) 回复唤醒 ACK
    protocol_frame_t ack;
    frame_build(&ack, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK, 0, 0, battery);
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
            // 发送的时候随机加个延时，避免长期占线
            // uint8_t delay_ms = random(0, 4);
            // vTaskDelay(pdMS_TO_TICKS(delay_ms));
            vTaskDelay(pdMS_TO_TICKS(10));
            Espnow.send(peer_mac, (uint8_t*)&frame, sizeof(frame));
        }

        vTaskDelay(pdMS_TO_TICKS(RADAR_SEND_INTERVAL_MS));
    }

    // 3) 退出：通知主机 + 关雷达
    sendEndFrame(peer_mac, SLAVE_FRAME_HEAD, battery);
    Radar.shutdown();
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}
// #endif

// ======================== 唤醒源 → 模式判断 ========================

static SysMode determineMode(WakeupSource wakeup, bool has_peer)
{
    SysMode mode = UNPAIRED_MODE;


// #ifdef OUTSIDE
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
// #endif

    // 工作模式需要已配对
    if (mode == WORK_MODE && !has_peer) {
        ESP_LOGW(MAIN_TAG, "Not paired, cannot enter WORK_MODE");
        mode = UNPAIRED_MODE;
    }

    return mode;
}

// ======================== 模式处理 ========================

static void handleMode(SysMode mode, uint8_t battery)
{
    switch (mode)
    {
    case FACTORY_RESET_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: FACTORY_RESET");
        for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_3);
        // 清除
// #ifdef INSIDE
//         Matcher.clear_slave_mac();
// #endif

// #ifdef OUTSIDE
        Matcher.clear_master_mac();
// #endif
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
        // TODO：这里需不要要用灯来指示配对过程，比如处于配对时用呼吸灯？
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


// #ifdef OUTSIDE
        // ---- 车外模块工作流程 ----
        // 1) Lora 初始化（被唤醒后需要就绪状态）
        Lora.setup();

        // 2) ESP-NOW 初始化
        Espnow.init();
        uint8_t master_mac[6]{};
        Matcher.load_master_mac(master_mac);
        Espnow.addPeer(master_mac);
        Espnow.recvStart();

        // 3) 回复 ACK → 雷达采集循环
        outside_work_mode(master_mac, battery);

        // 4) 清理
        Espnow.recvStop();
        Espnow.deinit();
        Lora.shutdown();
// #endif

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
// #ifdef INSIDE
//     bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();
// #endif

// #ifdef OUTSIDE
    bool has_peer = Matcher.has_master_mac();
// #endif


    // TODO：这里是不是加个else直接进入unpair模式？

    // 判断模式 → 执行 → 睡眠
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode, bat);
    Power.deep_sleep();
}

void loop() { }
