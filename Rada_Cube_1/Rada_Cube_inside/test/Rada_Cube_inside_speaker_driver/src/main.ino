#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "lora.h"
#include "sensor.h"
#include "mac_match.h"
#include "protocol.h"
#include "espnow.h"
#include "speaker_controller.h"

#ifndef LOW_BATTERY_PERCENT
#define LOW_BATTERY_PERCENT             20
#endif
#ifndef AUDIO_PROMPT_QUEUE_LENGTH
#define AUDIO_PROMPT_QUEUE_LENGTH       8
#endif
#ifndef AUDIO_PROMPT_DRAIN_TIMEOUT_MS
#define AUDIO_PROMPT_DRAIN_TIMEOUT_MS   5000
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
#ifndef DISTANCE_BEEP_CONFIRM_COUNT
#define DISTANCE_BEEP_CONFIRM_COUNT     2
#endif
#ifndef DISTANCE_BEEP_MIN_SWITCH_MS
#define DISTANCE_BEEP_MIN_SWITCH_MS     350
#endif

struct SensorLinkState {
    bool woke = false;
    bool lost_announced = false;
    uint32_t last_data_ms = 0;
    uint32_t last_fault_prompt_ms = 0;
    uint8_t invalid_count = 0;
};


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
SpeakerController Speaker;

struct AudioPrompt {
    AudioId audio = AudioId::None;
    SpeakerVolumeLevel volume = SpeakerVolumeLevel::Default;
};

static AudioPrompt audio_prompt_queue[AUDIO_PROMPT_QUEUE_LENGTH];
static uint8_t audio_prompt_head = 0;
static uint8_t audio_prompt_tail = 0;
static uint8_t audio_prompt_count = 0;
static bool audio_prompt_playing = false;
static uint32_t audio_prompt_started_ms = 0;
static AudioId current_distance_beep = AudioId::None;
static AudioId pending_distance_beep = AudioId::None;
static uint8_t pending_distance_beep_count = 0;
static uint32_t last_distance_beep_switch_ms = 0;

static const char* audioIdName(AudioId audio)
{
    switch (audio) {
    case AudioId::BeepSlow:
        return "BeepSlow";
    case AudioId::BeepMediumSlow:
        return "BeepMediumSlow";
    case AudioId::BeepMedium:
        return "BeepMedium";
    case AudioId::BeepFast:
        return "BeepFast";
    case AudioId::BeepContinuous:
        return "BeepContinuous";
    case AudioId::Boot:
        return "Boot";
    case AudioId::PairOk:
        return "PairOk";
    case AudioId::PairFail:
        return "PairFail";
    case AudioId::ConnectionLost:
        return "ConnectionLost";
    case AudioId::LowBattery:
        return "LowBattery";
    case AudioId::Fault:
        return "Fault";
    case AudioId::None:
    default:
        return "None";
    }
}

static bool audioPromptPush(AudioId audio,
                            SpeakerVolumeLevel volume = SpeakerVolumeLevel::Default)
{
    if (audio == AudioId::None || audio_prompt_count >= AUDIO_PROMPT_QUEUE_LENGTH) {
        ESP_LOGW(MAIN_TAG, "Audio prompt dropped: %s", audioIdName(audio));
        return false;
    }

    audio_prompt_queue[audio_prompt_tail].audio = audio;
    audio_prompt_queue[audio_prompt_tail].volume = volume;
    audio_prompt_tail = (audio_prompt_tail + 1) % AUDIO_PROMPT_QUEUE_LENGTH;
    audio_prompt_count++;
    ESP_LOGI(MAIN_TAG, "Audio prompt queued: %s", audioIdName(audio));
    return true;
}

static void audioPromptUpdate()
{
    if (!Speaker.isBegun()) {
        return;
    }

    if (audio_prompt_playing) {
        if (Speaker.currentMode() == SpeakerMode::Silent &&
            millis() - audio_prompt_started_ms < (SPEAKER_TASK_IDLE_DELAY_MS * 3)) {
            return;
        }
        if (Speaker.currentMode() != SpeakerMode::Silent) {
            return;
        }
        audio_prompt_playing = false;
        Speaker.setVolume(SpeakerVolumeLevel::Medium);
    }

    if (audio_prompt_count == 0) {
        return;
    }

    AudioPrompt prompt = audio_prompt_queue[audio_prompt_head];
    audio_prompt_head = (audio_prompt_head + 1) % AUDIO_PROMPT_QUEUE_LENGTH;
    audio_prompt_count--;

    if (current_distance_beep != AudioId::None) {
        current_distance_beep = AudioId::None;
        pending_distance_beep = AudioId::None;
        pending_distance_beep_count = 0;
        last_distance_beep_switch_ms = millis();
    }

    Speaker.setVolume(prompt.volume);
    if (Speaker.playOnce(prompt.audio)) {
        audio_prompt_playing = true;
        audio_prompt_started_ms = millis();
        ESP_LOGI(MAIN_TAG, "Audio prompt playing: %s", audioIdName(prompt.audio));
    } else {
        Speaker.setVolume(SpeakerVolumeLevel::Medium);
    }
}

static bool audioPromptBusy()
{
    return audio_prompt_playing || audio_prompt_count > 0;
}

static void audioPromptDrain(uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    while (audioPromptBusy() && millis() - start_ms < timeout_ms) {
        audioPromptUpdate();
        vTaskDelay(pdMS_TO_TICKS(SPEAKER_TASK_IDLE_DELAY_MS));
    }
}

static void queueConnectionLostPrompt()
{
    for (uint8_t i = 0; i < CONNECTION_LOST_REPEAT_COUNT; i++) {
        audioPromptPush(AudioId::ConnectionLost, SpeakerVolumeLevel::Medium);
    }
}

static void queueShutdownPrompt()
{
    // Temporary shutdown prompt: reuse boot.wav at a lower volume until shutdown.wav exists.
    audioPromptPush(AudioId::Boot, SpeakerVolumeLevel::Low);
}

static AudioId distanceToBeepAudio(uint16_t dist_cm)
{
    if (dist_cm == UINT16_MAX || dist_cm > 150) {
        return AudioId::None;
    }
    if (dist_cm > 120) {
        return AudioId::BeepSlow;
    }
    if (dist_cm > 90) {
        return AudioId::BeepMediumSlow;
    }
    if (dist_cm > 60) {
        return AudioId::BeepMedium;
    }
    if (dist_cm > 30) {
        return AudioId::BeepFast;
    }
    return AudioId::BeepContinuous;
}

static void stopDistanceBeep()
{
    if (current_distance_beep == AudioId::None) {
        return;
    }

    Speaker.stop();
    ESP_LOGI(MAIN_TAG, "Distance beep stopped");
    current_distance_beep = AudioId::None;
    pending_distance_beep = AudioId::None;
    pending_distance_beep_count = 0;
    last_distance_beep_switch_ms = millis();
}

static void updateDistanceBeep(uint16_t min_dist_cm)
{
    if (!Speaker.isBegun()) {
        return;
    }

    const AudioId target = distanceToBeepAudio(min_dist_cm);
    if (target == current_distance_beep) {
        pending_distance_beep = AudioId::None;
        pending_distance_beep_count = 0;
        return;
    }

    if (target != pending_distance_beep) {
        pending_distance_beep = target;
        pending_distance_beep_count = 1;
        return;
    }

    if (pending_distance_beep_count < DISTANCE_BEEP_CONFIRM_COUNT) {
        pending_distance_beep_count++;
        return;
    }

    if (millis() - last_distance_beep_switch_ms < DISTANCE_BEEP_MIN_SWITCH_MS) {
        return;
    }

    if (target == AudioId::None) {
        stopDistanceBeep();
        return;
    }

    if (Speaker.playLoop(target)) {
        current_distance_beep = target;
        pending_distance_beep = AudioId::None;
        pending_distance_beep_count = 0;
        last_distance_beep_switch_ms = millis();
        ESP_LOGI(MAIN_TAG, "Distance beep playing: %s, dist=%u cm",
                 audioIdName(target), min_dist_cm);
    }
}

static void pairAudioCallback(uint8_t slave_id, const uint8_t mac[6], void* context)
{
    (void)slave_id;
    (void)mac;
    (void)context;
    audioPromptPush(AudioId::PairOk, SpeakerVolumeLevel::Medium);
    audioPromptUpdate();
}

static void enterDeepSleepWithAudio()
{
    if (Speaker.isBegun()) {
        Speaker.setKeepOutputAlive(false);
        queueShutdownPrompt();
        audioPromptDrain(AUDIO_PROMPT_DRAIN_TIMEOUT_MS);
        Speaker.stop();
        Speaker.end();
    }

    Power.deep_sleep();
}

// ======================== RTOS global variables ========================

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
    // Led.led_on();
    bool led_flag = true;

    while (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
           digitalRead(DEV_BUTTON_PIN)  == BUTTON_PRESSED)
    {
        audioPromptUpdate();
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
static bool isRadarDistanceValid(uint16_t dist)
{
    return dist > 0 && dist <= DIST_MAX_CM;
}

static void queueFaultPromptWithCooldown(SensorLinkState& sensor)
{
    const uint32_t now = millis();
    if (sensor.last_fault_prompt_ms == 0 ||
        now - sensor.last_fault_prompt_ms >= SENSOR_FAULT_COOLDOWN_MS) {
        audioPromptPush(AudioId::Fault, SpeakerVolumeLevel::Medium);
        sensor.last_fault_prompt_ms = now;
    }
}

static void updateSensorDataState(SensorLinkState& sensor, const protocol_frame_t& data)
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
        queueFaultPromptWithCooldown(sensor);
    }
}

static void checkSensorConnectionLost(SensorLinkState& sensor)
{
    if (!sensor.woke || sensor.lost_announced || sensor.last_data_ms == 0) {
        return;
    }

    if (millis() - sensor.last_data_ms >= CONNECTION_LOST_TIMEOUT_MS) {
        sensor.lost_announced = true;
        sensor.invalid_count = 0;
        queueConnectionLostPrompt();
    }
}

static void inside_work_mode(uint8_t* a_mac, uint8_t* b_mac)
{
    // 1) Lora sends a wake-up frame and waits for the slave ESP-NOW to reply WAKE_ACK
    bool slave_a_woke = false;
    bool slave_b_wake = false;
    bool woke = false;
    espnow_msg_t msg;
    SensorLinkState sensor_a;
    SensorLinkState sensor_b;

    // ================ Put the level switching in the loop outside and add a 100ms delay. The previous 10ms delay is not enough, which will lead to incomplete frame transmission =================
    Lora.enable_ce();
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int retry = 0; retry < WAKE_MAX_RETRY && !woke; retry++) {
        audioPromptUpdate();
        Lora.sendWakeFrame();
        ESP_LOGI(MAIN_TAG, "Wake attempt %d/%d", retry + 1, WAKE_MAX_RETRY);

        for (int t = 0; t < WAKE_POLL_ROUNDS && !woke; t++) {
            audioPromptUpdate();
            vTaskDelay(pdMS_TO_TICKS(WAKE_POLL_INTERVAL_MS));
            while (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
                    // Here determine which slave machine returns the message
                    if(memcmp(msg.src_mac, a_mac, 6) == 0) {
                        if (!slave_a_woke) {
                            audioPromptPush(AudioId::PairOk, SpeakerVolumeLevel::Medium);
                        }
                        slave_a_woke = true;
                        sensor_a.woke = true;
                        ESP_LOGI(MAIN_TAG, "Slave A woke up");
                    }
                    else if(memcmp(msg.src_mac, b_mac, 6) == 0) {
                        if (!slave_b_wake) {
                            audioPromptPush(AudioId::PairOk, SpeakerVolumeLevel::Medium);
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

    // Wake-up failure handling
    // TODO: The wake-up failed. It may be that one of the two slaves wakes up and the other has a problem. It is still necessary to send a work end frame to let the awakened slave stop working.
    if (!woke) {
        ESP_LOGE(MAIN_TAG, "Failed to wake slave after %d retries", WAKE_MAX_RETRY);
        queueConnectionLostPrompt();
        
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


    // 3) Work cycle: read the latest radar data → buzzer prompt
    uint32_t work_start = millis();

    protocol_frame_t slave_a_data = {0};
    protocol_frame_t slave_b_data  = {0};
    uint16_t dist_min = 0;
    bool exit_flag = false;

    // Hysteresis interval, used to record the last mode to prevent frequent switching
    // BuzzerMode current_mode = SILENT_MODE;

    while (true) {
        audioPromptUpdate();

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
                    updateSensorDataState(sensor_a, slave_a_data);
                    ESP_LOGI(SLAVE_A_TAG, "slave_A: dist=%d mm, angle=%.2f deg"
                             , slave_a_data.dist, slave_a_data.angle * 0.01f);
                }
                else if(memcmp(msg.src_mac, b_mac, 6) == 0)
                {
                    memcpy(&slave_b_data, msg.data, sizeof(protocol_frame_t));
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

        // After receiving the exit frame, jump out of the loop
        if(exit_flag)
        {
            break;
        }

        // Mode judgment
        // TODO: If the slave data has not been updated for a long time, should it be considered that it has lost contact?
        checkSensorConnectionLost(sensor_a);
        checkSensorConnectionLost(sensor_b);

        uint16_t dist_a = slave_a_data.dist;
        uint16_t dist_b = slave_b_data.dist;

        // Determine whether the data is valid
        // TODO: Do I need to add a maximum limit?
        bool valid_a = isRadarDistanceValid(dist_a);
        bool valid_b = isRadarDistanceValid(dist_b);

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

        if (audioPromptBusy()) {
            stopDistanceBeep();
        } else {
            updateDistanceBeep(min_dist);
        }

        vTaskDelay(pdMS_TO_TICKS(WORK_POLL_INTERVAL_MS));
    }
    // Change to actively kill the process
    stopDistanceBeep();
    vTaskDelay(pdMS_TO_TICKS(200));
    // 4) Exit: stop beeping + notify the slave of the end
    // Beeper.beep_stop();
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
        // for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_3);
        // Clear
        Matcher.clear_slave_mac();
        Lora.setup();               // Lora needs to be initialized before clearing the configuration
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO: Should I start pairing directly in unpair mode?
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        // for (int i = 0; i < 2; i++) Led.blink(LED_PERIOD_2);
        break;

    case PAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: PAIRED");
        // TODO: Is it necessary to use a light to indicate the pairing process, such as a breathing light during pairing?
        // Led.blink(LED_PERIOD_1);
        if (!Matcher.pair(PAIR_MAX_RETRY, pairAudioCallback, nullptr)) {
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
        } else {
            // for (int i = 0; i < 3; i++) Led.blink(LED_PERIOD_1); // Pairing success prompt
        }
        break;

    case TEST_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: TEST");
        // TODO: Test mode, used for development and factory testing of hardware
        // Led.blink(LED_PERIOD_2);
        break;

    case CONFIG_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: CONFIG");
        // TODO: Configuration mode, reserved (OTA, etc.)
        // Led.blink(LED_PERIOD_2);
        break;

    case WORK_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: WORK");
        Speaker.setKeepOutputAlive(true);
        // Led.led_on();

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

    // Initialize button pins & detect wake-up reasons
    Power.wakeup_gpio_init();
    Power.detectWakeupSource();
    WakeupSource wakeup = Power.getWakeupSource();

    // Led.led_init();
    Power.power_init();

    // After turning on the power of Lora, I found that without turning on the power for the first time, the enable pin of the power switch will be stuck at about 0.7V instead of 3.3V after adding a pull-up resistor, causing Lora to not work.
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);

    if (Speaker.begin()) {
        Speaker.setVolume(SpeakerVolumeLevel::Medium);
        audioPromptPush(AudioId::Boot, SpeakerVolumeLevel::Medium);
        audioPromptUpdate();
    } else {
        ESP_LOGE(MAIN_TAG, "Speaker init failed");
    }

    // Battery power detection
    uint8_t bat = Power.get_battery_value();
    if (bat <= LOW_BATTERY_PERCENT) {
        audioPromptPush(AudioId::LowBattery, SpeakerVolumeLevel::Medium);
    }
    // This can be adjusted a little higher. When the bat is 0, it may not be able to be powered on.
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        enterDeepSleepWithAudio();
        return;
        // TODO: Is it possible to send a warning when the battery is too low? Like a flashing light or a buzzer?
    }

    // Read paired MAC (if available)
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();

    // TODO: Should I add an else here to directly enter the unpair mode?

    // Judgment mode → Execute → Sleep
    SysMode mode = determineMode(wakeup, has_peer);
    handleMode(mode);
    enterDeepSleepWithAudio();
}

void loop() { }
