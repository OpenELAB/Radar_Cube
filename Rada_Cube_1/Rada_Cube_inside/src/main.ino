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
#define SENSOR_INVALID_MAX_COUNT        15
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
    uint8_t recovery_valid_count = 0;
    bool distance_fault = false;
    protocol_frame_t latest_data{};

    bool isActive() const
    {
        return woke && !lost_announced && !distance_fault;
    }

    bool hasValidDistance() const
    {
        return isActive() &&
               latest_data.dist > 0 &&
               latest_data.dist <= DIST_MAX_CM;
    }
};

struct SensorChanges {
    bool link_lost = false;
    bool link_restored = false;
    bool fault_started = false;
    bool fault_cleared = false;

    void merge(const SensorChanges& other)
    {
        link_lost = link_lost || other.link_lost;
        link_restored = link_restored || other.link_restored;
        fault_started = fault_started || other.fault_started;
        fault_cleared = fault_cleared || other.fault_cleared;
    }
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
        if (pair_context->left_paired) {
            return;
        }
        pair_context->left_paired = true;
    } else if (slave_id == SLAVE_B_ID) {
        if (pair_context->right_paired) {
            return;
        }
        pair_context->right_paired = true;
    } else {
        return;
    }

    if (feedback_ready) {
        const FeedbackSensorSet paired_sensors =
            toFeedbackSensorSet(pair_context->left_paired,
                                pair_context->right_paired);

        // Wait for the previous pairing feedback to end, and then play the successful feedback from the slave this time.
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);

        // PairSucceeded event: Update the currently successfully paired module collection and corresponding lighting effects.
        Feedback.onPairSucceededEvent(paired_sensors);
        // PairSuccessTone event: Play the slave pairing success tone.
        Feedback.onPairSuccessToneEvent();
    }
}

static void enterDeepSleepWithFeedback(FeedbackScene current_scene)
{
    if (feedback_ready) {
        // Shutdown event: A system shutdown event occurs.
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

static SensorChanges updateSensorDataState(
    SensorLinkState& sensor,
    const protocol_frame_t& data,
    uint32_t now_ms)
{
    SensorChanges changes;
    const bool valid_distance = isRadarDistanceValid(data.dist);
    sensor.last_data_ms = now_ms;
    // A verified radar data frame is enough to prove that the communication link is online, even if the distance value in it is invalid.
    changes.link_restored = sensor.lost_announced;
    sensor.lost_announced = false;

    if (!sensor.distance_fault) {
        sensor.recovery_valid_count = 0;

        if (valid_distance) {
            sensor.invalid_count = 0;
            return changes;
        }

        if (sensor.invalid_count < SENSOR_INVALID_MAX_COUNT) {
            sensor.invalid_count++;
        }
        if (sensor.invalid_count >= SENSOR_INVALID_MAX_COUNT) {
            sensor.distance_fault = true;
            sensor.recovery_valid_count = 0;
            changes.fault_started = true;
        }
        return changes;
    }

    sensor.invalid_count = 0;

    if (!valid_distance) {
        sensor.recovery_valid_count = 0;
        return changes;
    }

    if (sensor.recovery_valid_count < SENSOR_INVALID_MAX_COUNT) {
        sensor.recovery_valid_count++;
    }
    if (sensor.recovery_valid_count >= SENSOR_INVALID_MAX_COUNT) {
        sensor.distance_fault = false;
        sensor.recovery_valid_count = 0;
        changes.fault_cleared = true;
    }

    return changes;
}

static SensorChanges updateSensorConnectionLost(
    SensorLinkState& sensor,
    uint32_t now_ms)
{
    SensorChanges changes;
    if (!sensor.woke || sensor.lost_announced || sensor.last_data_ms == 0) {
        return changes;
    }

    if (now_ms - sensor.last_data_ms >= CONNECTION_LOST_TIMEOUT_MS) {
        sensor.lost_announced = true;
        sensor.invalid_count = 0;
        sensor.recovery_valid_count = 0;
        sensor.distance_fault = false;
        changes.link_lost = true;
    }
    return changes;
}

static void inside_work_mode(uint8_t* a_mac, uint8_t* b_mac)
{
    // 1) Lora sends a wake-up frame and waits for the slave ESP-NOW to reply WAKE_ACK
    bool pending_wake_success_tone = false;
    bool pending_wake_ok_tone = false;
    espnow_msg_t msg;
    SensorLinkState sensor_a;
    SensorLinkState sensor_b;

    // ================ Put the level switching in the loop outside and add a 100ms delay. The previous 10ms delay is not enough, which will lead to incomplete frame transmission =================
    Lora.enable_ce();
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int retry = 0;
         retry < WAKE_MAX_RETRY && !(sensor_a.woke && sensor_b.woke);
         retry++) {
        Lora.sendWakeFrame();
        ESP_LOGI(MAIN_TAG, "Wake attempt %d/%d", retry + 1, WAKE_MAX_RETRY);

        for (int t = 0;
             t < WAKE_POLL_ROUNDS && !(sensor_a.woke && sensor_b.woke);
             t++) {
            vTaskDelay(pdMS_TO_TICKS(WAKE_POLL_INTERVAL_MS));
            while (Espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
                    // Here determine which slave machine returns the message
                    if(memcmp(msg.src_mac, a_mac, 6) == 0) {
                        if (!sensor_a.woke) {
                            const bool second_wake = sensor_b.woke;
                            sensor_a.woke = true;
                            if (feedback_ready) {
                                const FeedbackSensorSet awake_sensors =
                                    toFeedbackSensorSet(sensor_a.woke,
                                                        sensor_b.woke);
                                // WakeSucceeded event: Update the currently woken module collection.
                                Feedback.onWakeSucceededEvent(awake_sensors);
                                if (second_wake) {
                                    pending_wake_success_tone = true;
                                    pending_wake_ok_tone = true;
                                } else {
                                    // WakeSuccessTone event: A unilateral wake-up success tone event occurs.
                                    Feedback.onWakeSuccessToneEvent();
                                }
                            }
                            ESP_LOGI(MAIN_TAG, "Slave A woke up");
                        }
                    }
                    else if(memcmp(msg.src_mac, b_mac, 6) == 0) {
                        if (!sensor_b.woke) {
                            const bool second_wake = sensor_a.woke;
                            sensor_b.woke = true;
                            if (feedback_ready) {
                                const FeedbackSensorSet awake_sensors =
                                    toFeedbackSensorSet(sensor_a.woke,
                                                        sensor_b.woke);
                                // WakeSucceeded event: Update the currently woken module collection.
                                Feedback.onWakeSucceededEvent(awake_sensors);
                                if (second_wake) {
                                    pending_wake_success_tone = true;
                                    pending_wake_ok_tone = true;
                                } else {
                                    // WakeSuccessTone event: A unilateral wake-up success tone event occurs.
                                    Feedback.onWakeSuccessToneEvent();
                                }
                            }
                            ESP_LOGI(MAIN_TAG, "Slave B woke up");
                        }
                    }
                    if (sensor_a.woke && sensor_b.woke) {
                        break;
                    }
                }
            }
        }
    }
    Lora.disable_ce();

    // Wake-up failure handling
    // TODO: The wake-up failed. It may be that one of the two slaves wakes up and the other has a problem. It is still necessary to send a work end frame to let the awakened slave stop working.
    if (!(sensor_a.woke && sensor_b.woke)) {
        ESP_LOGE(MAIN_TAG, "Failed to wake slave after %d retries", WAKE_MAX_RETRY);
        if (feedback_ready) {
            // WakeTimedOut event: A wake-up timeout event occurs.
            Feedback.onWakeTimedOutEvent(
                toFeedbackSensorSet(sensor_a.woke, sensor_b.woke));
        }
        

        // If wake-up fails, a stop working frame is sent to ensure that all slave machines are shut down.
        sendEndFrame(a_mac, MASTER_FRAME_HEAD);
        sendEndFrame(b_mac, MASTER_FRAME_HEAD);
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
        
        return;
    }
    ESP_LOGI(MAIN_TAG, "Slave woke up");

    

    // Wake-up success processing: Give the user clear success feedback
    // Play a rising tone to send an "Active Positive" signal to the user, indicating that the radar has been activated.


    // After successful wake-up, you can turn off Lora to save power.
    Lora.shutdown();

    // 2) Create buzzer task
    // Initialize mutex lock
 

    // 3) Work cycle: read the latest radar data → buzzer prompt
    uint32_t work_start = millis();

    bool exit_flag = false;
    bool wait_exit_feedback = false;
    bool distance_feedback_started = false;
    FeedbackSensorSet last_active_sensors = FeedbackSensorSet::None;
    FeedbackDistanceLevel last_distance_level = FeedbackDistanceLevel::Safe;
    FeedbackDistanceLevel pending_distance_level = FeedbackDistanceLevel::Safe;
    uint8_t pending_distance_level_count = 0;
    uint32_t last_distance_level_switch_ms = 0;

    // Hysteresis interval, used to record the last mode to prevent frequent switching

    while (true) {
        if (feedback_ready && !Feedback.isBusy()) {
            if (pending_wake_success_tone) {
                // WakeSuccessTone event: A delay-scheduled unilateral wake-up success tone event occurs.
                Feedback.onWakeSuccessToneEvent();
                pending_wake_success_tone = false;
            } else if (pending_wake_ok_tone) {
                // WakeCompletedTone event: A bilateral wake-up completion tone event occurs.
                Feedback.onWakeCompletedToneEvent();
                pending_wake_ok_tone = false;
            }
        }

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
        SensorChanges left_changes;
        SensorChanges right_changes;

        while(Espnow.read(&msg))
        {
            if(frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA))
            {
                // Determine which slave returns the data
                if(memcmp(msg.src_mac, a_mac, 6) == 0)
                {
                    memcpy(&sensor_a.latest_data,
                           msg.data,
                           sizeof(protocol_frame_t));
                    left_changes.merge(updateSensorDataState(
                        sensor_a,
                        sensor_a.latest_data,
                        millis()));
                    ESP_LOGI(SLAVE_A_TAG, "slave_A: dist=%d mm, angle=%.2f deg"
                             , sensor_a.latest_data.dist,
                             sensor_a.latest_data.angle * 0.01f);
                }
                else if(memcmp(msg.src_mac, b_mac, 6) == 0)
                {
                    memcpy(&sensor_b.latest_data,
                           msg.data,
                           sizeof(protocol_frame_t));
                    right_changes.merge(updateSensorDataState(
                        sensor_b,
                        sensor_b.latest_data,
                        millis()));
                    ESP_LOGI(SLAVE_B_TAG, "slave_B: dist=%d mm, angle=%.2f deg"
                             , sensor_b.latest_data.dist,
                             sensor_b.latest_data.angle * 0.01f);
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
        left_changes.merge(updateSensorConnectionLost(sensor_a, now_ms));
        right_changes.merge(updateSensorConnectionLost(sensor_b, now_ms));

        const bool both_sensor_lost =
            sensor_a.lost_announced && sensor_b.lost_announced;

        const FeedbackSensorSet active_sensors =
            toFeedbackSensorSet(sensor_a.isActive(),
                                sensor_b.isActive());

        if (both_sensor_lost &&
            (left_changes.link_lost || right_changes.link_lost)) {
            if (feedback_ready) {
                // BothLinksLost event: A bilateral link loss event occurred.
                Feedback.onBothLinksLostEvent();
            }
            wait_exit_feedback = true;
            exit_flag = true;
        } else if (feedback_ready) {
            if (left_changes.link_lost || right_changes.link_lost) {
                // LinkLost event: Update feedback based on the set of active modules after the link was lost.
                Feedback.onLinkLostEvent(active_sensors);
            }
            // Removal events take priority over recovery events to prevent the sensor from returning online during the same polling cycle
            // When entering a fault, it is briefly displayed as recovered by the RGB light.
            if (left_changes.fault_started || right_changes.fault_started) {
                // DistanceSensorFault event: A distance sensor fault event occurs.
                Feedback.onDistanceSensorFaultEvent(
                    toFeedbackSensorSet(sensor_a.distance_fault,
                                        sensor_b.distance_fault),
                    active_sensors);
            }
            const bool active_link_restored =
                (left_changes.link_restored && !sensor_a.distance_fault) ||
                (right_changes.link_restored && !sensor_b.distance_fault);
            if (active_link_restored) {
                // LinkRestored event: Update feedback based on the restored active module collection.
                Feedback.onLinkRestoredEvent(active_sensors);
            }
        }

        if (exit_flag) {
            break;
        }

        if (left_changes.fault_cleared || right_changes.fault_cleared) {
            // Normal distance feedback will be restored only after confirming that the set number of consecutive valid distance frames has been reached.
            distance_feedback_started = false;
        }

        uint16_t min_dist = UINT16_MAX;
        if (sensor_a.hasValidDistance()) {
            min_dist = sensor_a.latest_data.dist;
        }
        if (sensor_b.hasValidDistance() &&
            sensor_b.latest_data.dist < min_dist) {
            min_dist = sensor_b.latest_data.dist;
        }

        // Add a hysteresis interval to prevent the buzzer mode from switching frequently

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
            !pending_wake_success_tone &&
            !pending_wake_ok_tone &&
            !Feedback.isBusy()) {
            // DistanceLevelChanged event: A distance level change event occurs.
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
    // 4) Exit: Notify the slave machine of the end. Distance feedback is uniformly stopped by subsequent shutdown events.
    sendEndFrame(a_mac, MASTER_FRAME_HEAD);
    sendEndFrame(b_mac, MASTER_FRAME_HEAD);
    if (wait_exit_feedback) {
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
    }
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
        // The first version of Feedback API has not yet defined factory reset feedback and only handles business actions.
        Matcher.clear_slave_mac();
        Lora.setup();               // Lora needs to be initialized before clearing the configuration
        Lora.clearConfigFlag();
        Lora.shutdown();
        break;
    
    // TODO: Should I start pairing directly in unpair mode?
    case UNPAIRED_MODE:
        ESP_LOGI(MAIN_TAG, "Mode: UNPAIRED");
        if (feedback_ready) {
            // UnpairedDetected event: An unpaired status detection event occurs.
            Feedback.onUnpairedDetectedEvent();
        }
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
        break;

    case PAIRED_MODE:
    {
        ESP_LOGI(MAIN_TAG, "Mode: PAIRING");
        if (feedback_ready) {
            // PairingStarted event: A pairing start event occurs.
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

        // Wait for the successful feedback from the last slave to end before submitting the final pairing result feedback.
        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);

        if (!pair_ok && paired_sensors != FeedbackSensorSet::Both) {
            ESP_LOGE(MAIN_TAG, "Pair failed, going to sleep");
            if (feedback_ready) {
                // PairingTimedOut event: A pairing timeout event occurs.
                Feedback.onPairingTimedOutEvent(paired_sensors);
            }
        } else {
            if (feedback_ready) {
                // PairBothSucceeded event: A bilateral pairing success event occurred.
                Feedback.onPairBothSucceededEvent();
            }
        }

        waitFeedbackOrTimeout(FEEDBACK_DEFAULT_TIMEOUT_MS);
        break;
    }

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

    // Battery power detection
    uint8_t bat = Power.get_battery_value();
    if (bat <= LOW_BATTERY_PERCENT) {
        // Main/power management is responsible for battery detection and low battery processing; the low battery log is recorded here first.
        ESP_LOGW(MAIN_TAG, "Low battery: %u%%", bat);
    }
    // This can be adjusted a little higher. When the bat is 0, it may not be able to be powered on.
    if (bat == 0) {
        ESP_LOGE(MAIN_TAG, "Battery empty, going to sleep");
        enterDeepSleepWithFeedback(FeedbackScene::Idle);
        return;
        // TODO: Is it possible to send a warning when the battery is too low? Like a flashing light or a buzzer?
    }

    // Read paired MAC (if available)
    bool has_peer = Matcher.has_slave_a_mac() && Matcher.has_slave_b_mac();

    // TODO: Should I add an else here to directly enter the unpair mode?

    // Judgment mode → Execute → Sleep
    SysMode mode = determineMode(wakeup, has_peer);
    FeedbackScene scene = modeToFeedbackScene(mode);
    if (feedback_ready) {
        // SystemBoot event: A system startup event occurs.
        Feedback.onSystemBootEvent(scene);
    }
    handleMode(mode);
    enterDeepSleepWithFeedback(scene);
}

void loop() { }
