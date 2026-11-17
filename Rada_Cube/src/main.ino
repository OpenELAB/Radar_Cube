#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "radar.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"
#include "espnow.h"

// System mode
enum SysMode {
    UNPAIRED_MODE,      // Not paired → sleep directly
    PAIRED_MODE,        // Pairing mode
    WORK_MODE,          // Working mode (Normal)
    TEST_MODE,          // Test mode (reserved)
    CONFIG_MODE,        // Configuration mode (reserved)
    FACTORY_RESET_MODE  // Restore factory settings
};

enum BuzzerMode{
    SILENT_MODE,        // Silent mode
    CONTINUOUS_MODE,    // Continuous tweeting
    FAST_MODE,          // Quick beep, period 0.25s
    MED_MODE,           // Medium speed chirp, period 0.5s
    SLOW_MODE,          // Slow chirping, cycle 1s
};


// ======================== Global instance ========================

// Serial port instance (extern statement in pins.h, defined here)
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

// ======================== RTOS global variables ========================
static volatile BuzzerMode buzzer_mode = SILENT_MODE;
static SemaphoreHandle_t buzzer_mutex = NULL;

// ======================== RTOS tasks ========================
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

            // If the mode changes, immediately stop the current beep and prepare to switch
            if(target != current_mode)
            {
                is_on = false;
                Beeper.beep_stop();
                last_toggle = xTaskGetTickCount();
                current_mode = target;
            }

            // State machine
            TickType_t now = xTaskGetTickCount();
            uint16_t period = Beeper.get_period(current_mode);

            switch(current_mode)
            {
                case SILENT_MODE:
                    //  Mute, keep the mute status above
                    break;
                case CONTINUOUS_MODE:
                    // Continuous tweeting
                    if(!is_on)
                    {
                        Beeper.beep();
                        is_on = true;
                    }
                    break;
                case FAST_MODE:
                case MED_MODE:
                case SLOW_MODE:
                // Non-blocking periodic chirping
                //TODO: Currently, non-blocking implementation is used. If you have any problems, you can try blocking implementation and directly use the buzzer period to calculate the delay.
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

// ======================== Auxiliary functions ========================

// Send end frame (resend END_SEND_COUNT times to ensure the other party receives it)
static void sendEndFrame(const uint8_t* peer_mac, uint8_t head)
{
    protocol_frame_t frame;
    frame_build(&frame, head, FRAME_END);
    for (int i = 0; i < END_SEND_COUNT; i++) {
        Espnow.send(peer_mac, (uint8_t*)&frame, sizeof(frame));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Check whether the end frame is received
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

// Wait for button release and return duration (ms)
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

// It is difficult to press two keys at the same time on the slave machine. I swapped config_mode and factor_reset_mode.

// Key → Mode judgment (USER and DEV are processed separately)
//   USER button: short press → WORK_MODE, long press (3s) → PAIRED_MODE
//   DEV button: short press → TEST_MODE, long press (3s) → CONFIG_MODE
//   Press two keys at the same time: → FACTORY_RESET_MODE
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

// ======================== INSIDE working mode ========================
#ifdef INSIDE
static void inside_work_mode(uint8_t* a_mac, uint8_t* b_mac)
{
    // 1) Lora sends a wake-up frame and waits for the slave ESP-NOW to reply WAKE_ACK
    bool slave_a_woke = false;
    bool slave_b_wake = false;
    bool woke = false;
    espnow_msg_t msg;

    // ================ Put the level switching in the loop outside and add a 100ms delay. The previous 10ms delay is not enough, which will lead to incomplete frame transmission =================
    Lora.enable_ce();
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int retry = 0; retry < WAKE_MAX_RETRY && !woke; retry++) {
        Lora.sendWakeFrame();
        ESP_LOGI(MAIN_TAG, "Wake attempt %d/%d", retry + 1, WAKE_MAX_RETRY);

        for (int t = 0; t < WAKE_POLL_ROUNDS && !woke; t++) {
            vTaskDelay(pdMS_TO_TICKS(WAKE_POLL_INTERVAL_MS));
            while (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
                    // Here determine which slave machine returns the message
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

    // Wake-up failure handling
    // TODO: The wake-up failed. It may be that one of the two slaves wakes up and the other has a problem. It is still necessary to send a work end frame to let the awakened slave stop working.
    if (!woke) {
        ESP_LOGE(MAIN_TAG, "Failed to wake slave after %d retries", WAKE_MAX_RETRY);
        
        // Suggestion: When wake-up fails, play a descending tone to tell the user "The external module of the vehicle has lost contact, do not reverse the vehicle!"
        Beeper.beeper_init();
        Beeper.play_fail_tone();

        // If wake-up fails, a stop working frame is sent to ensure that all slave machines are shut down.
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    

    // Wake-up success processing: Give the user clear success feedback
    // Play a rising tone to send an "Active Positive" signal to the user, indicating that the radar has been activated.
    Beeper.beeper_init();
    Beeper.play_success_tone();

    // After successful wake-up, you can turn off Lora to save power.
    Lora.shutdown();

    // 2) Create buzzer task
    // Initialize mutex lock
    if(buzzer_mutex == NULL)
    {
        buzzer_mutex = xSemaphoreCreateMutex();
    }
    // Create a buzzer task
    TaskHandle_t beeper_handle = NULL;
    xTaskCreate(buzzer_task, "beeper", 2048, NULL, 2, &beeper_handle);

    // 3) Work cycle: read the latest radar data → buzzer prompt
    uint32_t work_start = millis();

    protocol_frame_t slave_a_data = {0};
    protocol_frame_t slave_b_data  = {0};
    uint16_t dist_min = 0;
    bool exit_flag = false;

    // Hysteresis interval, used to record the last mode to prevent frequent switching
    BuzzerMode current_mode = SILENT_MODE;

    while (true) {
        // Exit condition 1: timeout
        if (millis() - work_start > WORK_TIMEOUT_MS) {
            ESP_LOGI(MAIN_TAG, "Work timeout, exiting");
            break;
        }

        // Exit condition 2: button pressed
        // TODO: Change to interrupt processing instead of polling detection? Now this is to handle key events in the work loop. It feels good, so I will do this for the time being.
        if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
            digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED) {
            ESP_LOGI(MAIN_TAG, "Button pressed, exiting work mode");
            break;
        }

        // Exit condition 3: End frame received
        // (OUTSIDE will send an END frame when exiting actively)

        // Check the data depth in the queue
        // ESP_LOGI(MAIN_TAG, "Queue depth: %d", Espnow.getQueueCount());

        // TODO: Read radar data in the queue. If there is always data in the queue, will it get stuck?
        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // Determine which slave returns the data
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

        // After receiving the exit frame, jump out of the loop
        if(exit_flag)
        {
            break;
        }

        // Mode judgment
        // TODO: If the slave data has not been updated for a long time, should it be considered that it has lost contact?
        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // Determine whether the data is valid
        // TODO: Do I need to add a maximum limit?
        bool valid_a = (dist_a > 0);
        bool valid_b = (dist_b > 0);

        // Get the judgment value
        uint16_t min_dist = UINT16_MAX;
        BuzzerMode new_mode;

        if(valid_a && valid_b)
        {
            // If both are valid, take the closer one
            min_dist = (dist_a < dist_b) ? dist_a : dist_b;
        }
        else if(valid_a)
        {
            // If only A is valid, take A
            min_dist = dist_a;
        }
        else if(valid_b)
        {
            // If only B is valid, take B
            min_dist = dist_b;
        }
        else
        {
            // If both are invalid, keep the initial value
        }

        // Add a hysteresis interval to prevent the buzzer mode from switching frequently
        // TODO: Let’s see if we need to change to filtering or add a layer of filtering.
        if(min_dist == UINT16_MAX)
        {
            // No valid data, keep mute
            new_mode = SILENT_MODE;
            current_mode = SILENT_MODE;
        }
        else
        {
            // Make hysteresis judgment based on the current mode (±10cm buffer zone)
            switch(current_mode)
            {
                case SILENT_MODE:
                {
                    // Currently in silent mode, entering slow mode requires less than 210cm
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
                    // It is currently in slow speed mode. It needs to be less than 160cm to enter the medium speed alarm mode, and it needs to be more than 230cm to return to silent mode.
                    if(min_dist < DIST_MID_CM - DIST_HYSTERESIS_CM)
                    {
                        // < 160cm, enter medium speed alarm
                        new_mode = MED_MODE;
                        current_mode = MED_MODE;
                    }
                    else if(min_dist > DIST_FAR_CM + DIST_HYSTERESIS_CM)
                    {
                        // > 230cm Return to mute
                        new_mode = SILENT_MODE;
                        current_mode = SILENT_MODE;
                    }
                    else
                    {
                        // 170~220cm, maintain slow alarm
                        new_mode = SLOW_MODE;
                    }
                    break;
                }
                case MED_MODE:
                {
                    // It is currently in the medium speed alarm mode. To enter the rapid alarm mode, it needs to be less than 110cm, and to return to the slow alarm mode, it needs to be more than 180cm.
                    if(min_dist < DIST_CLOSE_CM - DIST_HYSTERESIS_CM)
                    {
                        // < 110cm, enter emergency alarm
                        new_mode = FAST_MODE;
                        current_mode = FAST_MODE;
                    }
                    else if(min_dist > DIST_MID_CM + DIST_HYSTERESIS_CM)
                    {
                        // > 180cm Return to slow alarm
                        new_mode = SLOW_MODE;
                        current_mode = SLOW_MODE;
                    }
                    else
                    {
                        // 120~170cm, maintain medium speed alarm
                        new_mode = MED_MODE;
                    }
                    break;
                }
                case FAST_MODE:
                {
                    // It is currently in rapid alarm mode. To enter the emergency alarm mode, it needs to be less than 60cm, and to return to the medium speed, it needs to be more than 130cm.
                    if(min_dist < DIST_DANGER_CM - DIST_HYSTERESIS_CM)
                    {
                        // Enter emergency alarm
                        new_mode = CONTINUOUS_MODE;
                        current_mode = CONTINUOUS_MODE;
                    }
                    else if(min_dist > DIST_CLOSE_CM + DIST_HYSTERESIS_CM)
                    {
                        // Return to medium speed alarm
                        new_mode = MED_MODE;
                        current_mode = MED_MODE;
                    }
                    else
                    {
                        // 70~120cm maintains rapid alarm
                        new_mode = FAST_MODE;
                    }
                    break;
                }
                case CONTINUOUS_MODE:
                {
                    //The current mode is emergency alarm mode. To return to urgent alarm mode, it needs to be greater than 80
                    if(min_dist > DIST_DANGER_CM + DIST_HYSTERESIS_CM)
                    {
                        // Return to urgent alarm
                        new_mode = FAST_MODE;
                        current_mode = FAST_MODE;
                    }
                    else
                    {
                        // < 70cm maintain emergency alarm
                        new_mode = CONTINUOUS_MODE;
                    }
                    break;
                }
                default:
                {
                    // Other modes should not appear. If they do, fall back to mute.
                    new_mode = SILENT_MODE;
                    current_mode = SILENT_MODE;
                }
            }
        }
        // Write buzzer task
        xSemaphoreTake(buzzer_mutex, portMAX_DELAY);
        buzzer_mode = new_mode;
        xSemaphoreGive(buzzer_mutex);

        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // Change to actively kill the process
    vTaskDelay(pdMS_TO_TICKS(200));
    if(beeper_handle != NULL)
    {
        vTaskDelete(beeper_handle);
        ESP_LOGI(MAIN_TAG, "Buzzer task deleted");
    }

    // 4) Exit: stop beeping + notify the slave of the end
    Beeper.beep_stop();
    sendEndFrame(a_mac, MASTER_FRAME_HEAD);
    sendEndFrame(b_mac, MASTER_FRAME_HEAD);
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}
#endif

// ======================== OUTSIDE working mode ========================
#ifdef OUTSIDE
static void outside_work_mode(uint8_t* peer_mac)
{
    // 1) Reply to wake up ACK
    protocol_frame_t ack;
    frame_build(&ack, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK);
    if (Espnow.send(peer_mac, (uint8_t*)&ack, sizeof(ack)) != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Failed to send WAKE_ACK");
    }

    // Lora is no longer needed, turn off the serial port to make way for the radar (share Serial1)
    LoraSerial.end();

    // 2) Initialize radar + send radar data periodically
    Radar.init();
    uint32_t work_start = millis();

    while (true) {
        // Exit condition 1: timeout
        if (millis() - work_start > WORK_TIMEOUT_MS) {
            ESP_LOGI(MAIN_TAG, "Work timeout, exiting");
            break;
        }

        // Exit condition 2: button pressed
        if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
            digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED) {
            ESP_LOGI(MAIN_TAG, "Button pressed, exiting work mode");
            break;
        }

        // Exit condition 3: Host end frame received
        {
            espnow_msg_t msg;
            if (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, MASTER_FRAME_HEAD, FRAME_END)) {
                    ESP_LOGI(MAIN_TAG, "Received END frame from master");
                    break;
                }
            }
        }

        // Analyze Radar + Send
        Radar.loop();
        RadarData rd;
        if (Radar.getData(&rd)) {
            protocol_frame_t frame;
            frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA,
                        rd.dist_mm, (int16_t)(rd.angle_deg * 100));
            // Add a random delay when sending to avoid being busy for a long time
            // uint8_t delay_ms = random(0, 4);
            // vTaskDelay(pdMS_TO_TICKS(delay_ms));
            vTaskDelay(pdMS_TO_TICKS(10));
            Espnow.send(peer_mac, (uint8_t*)&frame, sizeof(frame));
        }

        vTaskDelay(pdMS_TO_TICKS(RADAR_SEND_INTERVAL_MS));
    }

    // 3) Exit: notify host + turn off radar
    sendEndFrame(peer_mac, SLAVE_FRAME_HEAD);
    Radar.shutdown();
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}
#endif

// ======================== Wake-up source → Mode judgment ========================

static SysMode determineMode(WakeupSource wakeup, bool has_peer)
{
    SysMode mode = UNPAIRED_MODE;

#ifdef INSIDE
    // In-car module: only key wake-up and power-on
    switch (wakeup) {
    case WAKEUP_USER_BUTTON:
    case WAKEUP_DEV_BUTTON:
    case WAKEUP_BOTH_BUTTONS:
        mode = detectButtonMode(wakeup);
        break;
    default:
        // Power-on reset → keep UNPAIRED_MODE → sleep directly
        break;
    }
#endif

#ifdef OUTSIDE
    // External module: key wake-up, Lora wake-up, power-on
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
        // Power-on reset → keep UNPAIRED_MODE → sleep directly
        break;
    }
#endif

    // Working mode needs to be paired
    if (mode == WORK_MODE && !has_peer) {
        ESP_LOGW(MAIN_TAG, "Not paired, cannot enter WORK_MODE");
        mode = UNPAIRED_MODE;
    }

    return mode;
}

// ======================== Pattern processing ========================

static void handleMode(SysMode mode)
{
    switch (mode)
    {
    case FACTORY_RESET_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: FACTORY_RESET");
        for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_3);
        // Clear
#ifdef INSIDE
        Matcher.clear_slave_mac();
#endif

#ifdef OUTSIDE
        Matcher.clear_master_mac();
#endif
        Lora.setup();               // Lora needs to be initialized before clearing the configuration
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO: Should I start pairing directly in unpair mode?
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        for (int i = 0; i < 2; i++) Led.blink(LED_PERIOD_2);
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRED");
        // TODO: Is it necessary to use a light to indicate the pairing process, such as a breathing light during pairing?
        Led.blink(LED_PERIOD_1);
        if (!Matcher.pair()) {
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
        } else {
            for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_1);  // Pairing success prompt
        }
        break;

    case TEST_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: TEST");
        // TODO: Test mode, used for development and factory testing of hardware
        Led.blink(LED_PERIOD_2);
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        // TODO: Configuration mode, reserved (OTA, etc.)
        Led.blink(LED_PERIOD_2);
        break;

    case WORK_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: WORK");
        Led.led_on();

#ifdef INSIDE
        // ---- In-vehicle module work flow ----
        // 1) Lora initialization + sending wake-up frame
        Lora.setup();

        // 2) ESP-NOW initialization
        Espnow.init();
        uint8_t a_mac[6]{};
        Matcher.load_slave_mac(a_mac, SLAVE_A_ID);
        uint8_t b_mac[6]{};
        Matcher.load_slave_mac(b_mac, SLAVE_B_ID);
        Espnow.addPeer(a_mac);
        Espnow.addPeer(b_mac);
        Espnow.recvStart();

        // 3) Wake up the slave → work cycle
        inside_work_mode(a_mac, b_mac);

        // 4) Clean up
        Espnow.recvStop();
        Espnow.deinit();
        Lora.shutdown();
#endif

#ifdef OUTSIDE
        // ---- Off-vehicle module work flow ----
        // 1) Lora initialization (needs ready state after waking up)
        Lora.setup();

        // 2) ESP-NOW initialization
        Espnow.init();
        uint8_t master_mac[6]{};
        Matcher.load_master_mac(master_mac);
        Espnow.addPeer(master_mac);
        Espnow.recvStart();

        // 3) Reply ACK → radar acquisition loop
        outside_work_mode(master_mac);

        // 4) Clean up
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

    // Initialize button pins & detect wake-up reasons
    Power.wakeup_gpio_init();
    Power.detectWakeupSource();
    WakeupSource wakeup = Power.getWakeupSource();

    Led.led_init();
    Power.power_init();

    // After turning on the power of Lora, I found that without turning on the power for the first time, the enable pin of the power switch will be stuck at about 0.7V instead of 3.3V after adding a pull-up resistor, causing Lora to not work.
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);

    // Battery power detection
    uint8_t bat = Power.get_battery_value();
    // This can be adjusted a little higher. When the bat is 0, it may not be able to be powered on.
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        Power.deep_sleep();
        // TODO: Is it possible to send a warning when the battery is too low? Like a flashing light or a buzzer?
    }

    // Read paired MAC (if available)
#ifdef INSIDE
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();
#endif

#ifdef OUTSIDE
    bool has_peer = Matcher.has_master_mac();
#endif


    // TODO: Should I add an else here to directly enter the unpair mode?

    // Judgment mode → Execute → Sleep
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    Power.deep_sleep();
}

void loop() { }
