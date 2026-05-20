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

enum BuzzerMode{
    SILENT_MODE,        // 静音模式
    CONTINUOUS_MODE,    // 连续鸣叫
    FAST_MODE,          // 快速鸣叫，周期0.25s
    MED_MODE,           // 中速鸣叫，周期0.5s
    SLOW_MODE,          // 慢速鸣叫，周期1s
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

// ======================== RTOS全局变量 ========================
static volatile BuzzerMode buzzer_mode = SILENT_MODE;
static SemaphoreHandle_t buzzer_mutex = NULL;

// ======================== RTOS任务 ========================
#ifdef INSIDE
    void buzzer_task(void* pvParameters)
    {
        BuzzerMode current_mode = SILENT_MODE;
        TickType_t last_toggle = 0;
        bool is_on = false;

        while(1)
        {
            xSemaphoreTake(buzzer_mutex, portMAX_DELAY);
            BuzzerMode target = buzzer_mode;
            xSemaphoreGive(buzzer_mutex);

            // 如果模式改变了，立即停止当前蜂鸣并准备切换
            if(target != current_mode)
            {
                is_on = false;
                Beeper.beep_stop();
                last_toggle = xTaskGetTickCount();
                current_mode = target;
            }

            // 状态机
            TickType_t now = xTaskGetTickCount();
            uint16_t period = Beeper.get_period(current_mode);

            switch(current_mode)
            {
                case SILENT_MODE:
                    //  静音,保持上面静音状态不变
                    break;
                case CONTINUOUS_MODE:
                    // 连续鸣叫
                    if(!is_on)
                    {
                        Beeper.beep();
                        is_on = true;
                    }
                    break;
                case FAST_MODE:
                case MED_MODE:
                case SLOW_MODE:
                // 非阻塞式周期鸣叫
                //TODO: 目前采用非阻塞式实现，有问题可以试试阻塞式实现，直接用蜂鸣器的周期来算延时
                    if(now - last_toggle >= pdMS_TO_TICKS(period / 2))
                    {
                        if(is_on)
                        {
                            Beeper.beep_stop();
                            is_on = false;
                        }
                        else
                        {
                            Beeper.beep();
                            is_on = true;
                        }
                        last_toggle = now;
                    }
                    break;
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

    }
#endif

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
#ifdef INSIDE
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
        
        // 建议：唤醒失败时，播放下降音调，告诉用户“车外模块失联了，不要倒车！”
        Beeper.beeper_init();
        Beeper.play_fail_tone();

        // 唤醒失败发送停止工作帧，确保从机全部关闭
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    

    // 唤醒成功处理：给用户明确的成功反馈
    // 播放一段上升音调，向用户发出 "Active Positive" 信号，代表雷达已启动
    Beeper.beeper_init();
    Beeper.play_success_tone();

    // 唤醒成功后可以关 Lora 省电
    Lora.shutdown();

    // 2) 创建蜂鸣器任务
    // 初始化互斥锁
    if(buzzer_mutex == NULL)
    {
        buzzer_mutex = xSemaphoreCreateMutex();
    }
    // 创建蜂鸣器任务
    TaskHandle_t beeper_handle = NULL;
    xTaskCreate(buzzer_task, "beeper", 2048, NULL, 2, &beeper_handle);

    // 3) 工作循环：读最新雷达数据 → 蜂鸣器提示
    uint32_t work_start = millis();

    protocol_frame_t slave_a_data = {0};
    protocol_frame_t slave_b_data  = {0};
    uint16_t dist_min = 0;
    bool exit_flag = false;

    // 迟滞区间，用来记录上一次是什么模式，防止频繁切换
    BuzzerMode current_mode = SILENT_MODE;

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
        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // 判断哪个从机返回的数据
                if(memcmp(msg.src_mac, a_mac, 6) == 0)
                {
                    memcpy(&slave_a_data, msg.data, sizeof(protocol_frame_t));
                    ESP_LOGI(SLAVE_A_TAG, "slave_A: dist=%d mm, angle=%.2f deg"
                             , slave_a_data.dist, slave_a_data.angle * 0.01f);
                }
                else if(memcmp(msg.src_mac, b_mac, 6) == 0)
                {
                    memcpy(&slave_b_data, msg.data, sizeof(protocol_frame_t));
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
        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // 判断数据是否有效
        // TODO：需不需要加上最大值限制？
        bool valid_a = (dist_a > 0);
        bool valid_b = (dist_b > 0);

        // 取判断值
        uint16_t min_dist = UINT16_MAX;
        BuzzerMode new_mode;

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
        if(min_dist == UINT16_MAX)
        {
            // 没有有效数据，保持静音
            new_mode = SILENT_MODE;
            current_mode = SILENT_MODE;
        }
        else
        {
            // 基于当前模式去做迟滞判断（±10cm的缓冲带）
            switch(current_mode)
            {
                case SILENT_MODE:
                {
                    // 当前是静音模式，进入到慢速模式需要小于210cm
                    if(min_dist < DIST_FAR_CM - DIST_HYSTERESIS_CM)
                    {
                        new_mode = SLOW_MODE;
                        current_mode = SLOW_MODE;
                    }
                    else
                    {
                        new_mode = SILENT_MODE;
                    }
                    break;
                }
                case SLOW_MODE:
                {
                    // 当前是慢速模式，进入中速报警模式需要小于160cm，回退到静音需要大于230cm
                    if(min_dist < DIST_MID_CM - DIST_HYSTERESIS_CM)
                    {
                        // < 160cm 进入中速报警
                        new_mode = MED_MODE;
                        current_mode = MED_MODE;
                    }
                    else if(min_dist > DIST_FAR_CM + DIST_HYSTERESIS_CM)
                    {
                        // > 230cm 回退到静音
                        new_mode = SILENT_MODE;
                        current_mode = SILENT_MODE;
                    }
                    else
                    {
                        // 170~220cm 维持在慢速报警
                        new_mode = SLOW_MODE;
                    }
                    break;
                }
                case MED_MODE:
                {
                    // 当前是中速报警模式，进入到急促报警模式需要小于110cm，回退到慢速报警需要大于180cm
                    if(min_dist < DIST_CLOSE_CM - DIST_HYSTERESIS_CM)
                    {
                        // < 110cm 进入急促报警
                        new_mode = FAST_MODE;
                        current_mode = FAST_MODE;
                    }
                    else if(min_dist > DIST_MID_CM + DIST_HYSTERESIS_CM)
                    {
                        // > 180cm 回退到慢速报警
                        new_mode = SLOW_MODE;
                        current_mode = SLOW_MODE;
                    }
                    else
                    {
                        // 120~170cm 维持在中速报警
                        new_mode = MED_MODE;
                    }
                    break;
                }
                case FAST_MODE:
                {
                    // 当前是急促报警模式，进入紧急报警模式需要小于60cm，回退到中速需要大于130cm
                    if(min_dist < DIST_DANGER_CM - DIST_HYSTERESIS_CM)
                    {
                        // 进入紧急报警
                        new_mode = CONTINUOUS_MODE;
                        current_mode = CONTINUOUS_MODE;
                    }
                    else if(min_dist > DIST_CLOSE_CM + DIST_HYSTERESIS_CM)
                    {
                        // 回退到中速报警
                        new_mode = MED_MODE;
                        current_mode = MED_MODE;
                    }
                    else
                    {
                        // 70~120cm 维持在急促报警
                        new_mode = FAST_MODE;
                    }
                    break;
                }
                case CONTINUOUS_MODE:
                {
                    //当前模式是紧急报警模式，回退到急促报警模式需要大于80
                    if(min_dist > DIST_DANGER_CM + DIST_HYSTERESIS_CM)
                    {
                        // 回退到急促报警
                        new_mode = FAST_MODE;
                        current_mode = FAST_MODE;
                    }
                    else
                    {
                        // < 70cm 维持在紧急报警
                        new_mode = CONTINUOUS_MODE;
                    }
                    break;
                }
                default:
                {
                    // 应该不会出现其他模式了，如果出现了就回退到静音
                    new_mode = SILENT_MODE;
                    current_mode = SILENT_MODE;
                }
            }
        }
        // 写入蜂鸣器任务
        xSemaphoreTake(buzzer_mutex, portMAX_DELAY);
        buzzer_mode = new_mode;
        xSemaphoreGive(buzzer_mutex);

        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // 改为主动杀死进程
    vTaskDelay(pdMS_TO_TICKS(200));
    if(beeper_handle != NULL)
    {
        vTaskDelete(beeper_handle);
        ESP_LOGI(MAIN_TAG, "Buzzer task deleted");
    }

    // 4) 退出：停蜂鸣 + 通知从机结束
    Beeper.beep_stop();
    sendEndFrame(a_mac, MASTER_FRAME_HEAD);
    sendEndFrame(b_mac, MASTER_FRAME_HEAD);
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

static void handleMode(SysMode mode)
{
    switch (mode)
    {
    case FACTORY_RESET_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: FACTORY_RESET");
        for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_3);
        // 清除
#ifdef INSIDE
        Matcher.clear_slave_mac();
#endif

#ifdef OUTSIDE
        Matcher.clear_master_mac();
#endif
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

#ifdef INSIDE
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
#endif

#ifdef OUTSIDE
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
        outside_work_mode(master_mac);

        // 4) 清理
        Espnow.recvStop();
        Espnow.deinit();
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
#ifdef INSIDE
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();
#endif

#ifdef OUTSIDE
    bool has_peer = Matcher.has_master_mac();
#endif


    // TODO：这里是不是加个else直接进入unpair模式？

    // 判断模式 → 执行 → 睡眠
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    Power.deep_sleep();
}

void loop() { }
