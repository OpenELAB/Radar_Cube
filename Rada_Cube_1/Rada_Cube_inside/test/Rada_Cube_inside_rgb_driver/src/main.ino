#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"
#include "espnow.h"
#include "rgb_led_controller.h"

// System mode
enum SysMode {
    UNPAIRED_MODE,      // Not paired → sleep directly
    PAIRED_MODE,        // Pairing mode
    WORK_MODE,          // Working mode (Normal)
    TEST_MODE,          // Test mode (reserved)
    CONFIG_MODE,        // Configuration mode (reserved)
    FACTORY_RESET_MODE  // Restore factory settings
};


// ======================== Global instance ========================

// Serial port instance (extern statement in pins.h, defined here)
HardwareSerial& LoraSerial = Serial1;
PowerManager Power;
LoraManager Lora;
EspNowManager Espnow;
MacMatch Matcher(Espnow);
static RgbLedController RgbLed;


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

// Wait for button release and return duration (ms)
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
        onWakeFail();
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        // Suggestion: When wake-up fails, play a descending tone to tell the user "The external module of the vehicle has lost contact, do not reverse the vehicle!"
        // Beeper.beeper_init();
        // Beeper.play_fail_tone();

        // If wake-up fails, a stop working frame is sent to ensure that all slave machines are shut down.
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    

    // Wake-up success processing: Give the user clear success feedback
    // Play a rising tone to send an "Active Positive" signal to the user, indicating that the radar has been activated.
    // Beeper.beeper_init();
    // Beeper.play_success_tone();

    // After successful wake-up, you can turn off Lora to save power.
    Lora.shutdown();

    // 2) Create buzzer task

    // 3) Work cycle: read the latest radar data → buzzer prompt
    uint32_t work_start = millis();

    protocol_frame_t slave_a_data = {0};
    protocol_frame_t slave_b_data  = {0};
    uint32_t slave_a_update_ms = 0;
    uint32_t slave_b_update_ms = 0;
    bool exit_flag = false;

    // Hysteresis interval, used to record the last mode to prevent frequent switching
    // BuzzerMode current_mode = SILENT_MODE;

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

        // TODO: Read radar data in the queue. If there is always data in the queue, will it get stuck?
        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // Determine which slave returns the data
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

        // After receiving the exit frame, jump out of the loop
        if(exit_flag)
        {
            break;
        }

        // Mode judgment
        // TODO: If the slave data has not been updated for a long time, should it be considered that it has lost contact?
        const uint32_t now_ms = millis();
        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // Determine whether the data is valid
        // TODO: Do I need to add a maximum limit?
        bool valid_a = (slave_a_update_ms != 0) &&
                       (now_ms - slave_a_update_ms <= DIST_DATA_TIMEOUT_MS) &&
                       (dist_a >= DIST_MIN_CM) &&
                       (dist_a <= DIST_MAX_CM);
        bool valid_b = (slave_b_update_ms != 0) &&
                       (now_ms - slave_b_update_ms <= DIST_DATA_TIMEOUT_MS) &&
                       (dist_b >= DIST_MIN_CM) &&
                       (dist_b <= DIST_MAX_CM);

        // Get the judgment value
        uint16_t min_dist = UINT16_MAX;
        // BuzzerMode new_mode;

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
        if (min_dist != UINT16_MAX) {
            updateParkingDistance(min_dist);
        } else {
            updateParkingDistance(DIST_MAX_CM);
        }


        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // Change to actively kill the process
    vTaskDelay(pdMS_TO_TICKS(200));
    sendEndFrame(a_mac, MASTER_FRAME_HEAD);
    sendEndFrame(b_mac, MASTER_FRAME_HEAD);
    ESP_LOGI(MAIN_TAG, "Work mode finished");
}

// ======================== Wake-up source → Mode judgment ========================

static SysMode determineMode(WakeupSource wakeup, bool has_peer)
{
    SysMode mode = UNPAIRED_MODE;

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
        // Clear
        Matcher.clear_slave_mac();
        Lora.setup();               // Lora needs to be initialized before clearing the configuration
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO: Should I start pairing directly in unpair mode?
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRED");
        // TODO: Is it necessary to use a light to indicate the pairing process, such as a breathing light during pairing?
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
        // TODO: Test mode, used for development and factory testing of hardware
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        // TODO: Configuration mode, reserved (OTA, etc.)
        break;

    case WORK_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: WORK");

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

    // Led.led_init();
    Power.power_init();

    RgbLed.begin();
    RgbLed.setBrightness(RgbBrightnessLevel::Medium);
    RgbLed.solid(RGB_COLOR_GREEN);

    // After turning on the power of Lora, I found that without turning on the power for the first time, the enable pin of the power switch will be stuck at about 0.7V instead of 3.3V after adding a pull-up resistor, causing Lora to not work.
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);

    // Battery power detection
    uint8_t bat = Power.get_battery_value();
    // This can be adjusted a little higher. When the bat is 0, it may not be able to be powered on.
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        beforeDeepSleep();
        Power.deep_sleep();
        // TODO: Is it possible to send a warning when the battery is too low? Like a flashing light or a buzzer?
    }

    // Read paired MAC (if available)
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();

    // TODO: Should I add an else here to directly enter the unpair mode?

    // Judgment mode → Execute → Sleep
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    beforeDeepSleep();
    Power.deep_sleep();
}

void loop() { }
