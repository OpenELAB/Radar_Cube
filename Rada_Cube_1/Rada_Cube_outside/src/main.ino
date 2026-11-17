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


// ======================== Global instance ========================

// Serial port instance (extern statement in pins.h, defined here)
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


// ======================== OUTSIDE working mode ========================
// #ifdef OUTSIDE
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
// #endif

// ======================== Wake-up source → Mode judgment ========================

static SysMode determineMode(WakeupSource wakeup, bool has_peer)
{
    SysMode mode = UNPAIRED_MODE;


// #ifdef OUTSIDE
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
// #endif

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
// #ifdef INSIDE
//         Matcher.clear_slave_mac();
// #endif

// #ifdef OUTSIDE
        Matcher.clear_master_mac();
// #endif
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


// #ifdef OUTSIDE
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
// #endif

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
// #ifdef INSIDE
//     bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();
// #endif

// #ifdef OUTSIDE
    bool has_peer = Matcher.has_master_mac();
// #endif


    // TODO: Should I add an else here to directly enter the unpair mode?

    // Judgment mode → Execute → Sleep
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    Power.deep_sleep();
}

void loop() { }
