#include "outside_state_controller.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "pins.h"
#include "protocol.h"

#ifndef RADAR_DATA_DEBUG
#define RADAR_DATA_DEBUG 1
#endif

static bool matchingSessionFrame(const espnow_msg_t& msg,
                                 const uint8_t peer_mac[6],
                                 uint8_t expected_head,
                                 frame_type_t expected_type,
                                 uint32_t session_id)
{
    if (memcmp(msg.src_mac, peer_mac, 6) != 0 ||
        !frame_validate(msg.data, msg.len, expected_head, expected_type)) {
        return false;
    }

    protocol_frame_t frame{};
    memcpy(&frame, msg.data, sizeof(frame));
    return frame_get_session(&frame) == session_id;
}

OutsideStateController::OutsideStateController(
    BleWakeScanner& scanner,
    PowerManager& power,
    EspNowManager& espnow,
    MacMatch& matcher,
    RadarModule& radar)
    : _scanner(scanner),
      _power(power),
      _espnow(espnow),
      _matcher(matcher),
      _radar(radar)
{
}

bool OutsideStateController::begin()
{
    if (!_scanner.begin() || !_power.buttonQueue()) return false;

    _event_set = xQueueCreateSet(5);
    if (!_event_set) return false;
    if (xQueueAddToSet(_scanner.eventQueue(), _event_set) != pdPASS ||
        xQueueAddToSet(_power.buttonQueue(), _event_set) != pdPASS) return false;

    if (!_power.enableAutoLightSleep()) {
        ESP_LOGE(MAIN_TAG, "Light-sleep is required but could not be enabled");
        return false;
    }
    return resumeStandby();
}

OutsideStandbyEvent OutsideStateController::waitForEvent(
    WakeupSource* button_source, BleWakeEvent* wake_event)
{
    if (!_event_set || !_standby) return OutsideStandbyEvent::Error;

    QueueSetMemberHandle_t ready = xQueueSelectFromSet(_event_set, portMAX_DELAY);
    if (ready == _scanner.eventQueue()) {
        BleWakeEvent event{};
        if (xQueueReceive(_scanner.eventQueue(), &event, 0) == pdTRUE) {
            if (wake_event) *wake_event = event;
            return OutsideStandbyEvent::BleWake;
        }
    } else if (ready == _power.buttonQueue()) {
        if (_power.readButtonEvent(button_source)) return OutsideStandbyEvent::Button;
    }
    return OutsideStandbyEvent::Error;
}

void OutsideStateController::pauseStandby()
{
    if (_standby) {
        _scanner.stop();
        _standby = false;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    _power.setWorkActive(true);
}

bool OutsideStateController::resumeStandby()
{
    _espnow.deinit();
    WiFi.mode(WIFI_OFF);
    _power.clearButtonEvents();
    _power.setWorkActive(false);
    _standby = _scanner.start();
    if (_standby) ESP_LOGI(MAIN_TAG, "State: BLE_STANDBY (Auto Light-sleep)");
    return _standby;
}

void OutsideStateController::sendControlFrame(
    const uint8_t* peer_mac, frame_type_t type, int repeat_count,
    uint32_t session_id)
{
    protocol_frame_t frame{};
    frame_build_session(&frame, SLAVE_FRAME_HEAD, type, session_id);
    for (int i = 0; i < repeat_count; ++i) {
        _espnow.send(peer_mac, reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
        if (i + 1 < repeat_count) vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void OutsideStateController::sendStandbyAckGrace(
    const uint8_t* peer_mac, uint32_t session_id)
{
    const uint32_t started = millis();
    uint32_t last_ack = 0;
    while (millis() - started < STANDBY_ACK_GRACE_MS) {
        if (last_ack == 0 || millis() - last_ack >= STANDBY_RETRY_INTERVAL_MS) {
            sendControlFrame(peer_mac, FRAME_STANDBY_ACK, 2, session_id);
            last_ack = millis();
        }

        espnow_msg_t msg{};
        while (_espnow.read(&msg)) {
            if (matchingSessionFrame(msg, peer_mac, MASTER_FRAME_HEAD,
                                     FRAME_STANDBY, session_id)) {
                sendControlFrame(peer_mac, FRAME_STANDBY_ACK, 2, session_id);
                last_ack = millis();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void OutsideStateController::runWork(bool started_by_ble, uint32_t wake_session)
{
    if (!_matcher.has_master_mac()) {
        ESP_LOGW(MAIN_TAG, "Ignoring work request: outside unit is not paired");
        resumeStandby();
        return;
    }

    pauseStandby();

    uint8_t master_mac[6]{};
    if (!_matcher.load_master_mac(master_mac)) {
        resumeStandby();
        return;
    }

    _espnow.init();
    if (!_espnow.addPeer(master_mac)) {
        ESP_LOGE(MAIN_TAG, "Unable to add paired inside unit as ESP-NOW peer");
        resumeStandby();
        return;
    }
    _espnow.recvStart();

    if (started_by_ble) {
        ESP_LOGI(MAIN_TAG, "Waiting for wake confirmation, session=%08lX",
                 static_cast<unsigned long>(wake_session));
        bool confirmed = false;
        bool cancelled = false;
        const uint32_t handshake_started = millis();

        while (!confirmed && !cancelled &&
               millis() - handshake_started < WAKE_CONFIRM_TIMEOUT_MS) {
            sendControlFrame(master_mac, FRAME_WAKE_ACK, 2, wake_session);

            const uint32_t round_started = millis();
            const uint32_t round_ms = WAKE_ACK_INTERVAL_MS + random(0, 60);
            while (millis() - round_started < round_ms) {
                espnow_msg_t msg{};
                while (_espnow.read(&msg)) {
                    if (matchingSessionFrame(msg, master_mac, MASTER_FRAME_HEAD,
                                             FRAME_WAKE_CONFIRM, wake_session)) {
                        confirmed = true;
                        break;
                    }
                    if (matchingSessionFrame(msg, master_mac, MASTER_FRAME_HEAD,
                                             FRAME_STANDBY, wake_session) ||
                        (memcmp(msg.src_mac, master_mac, 6) == 0 &&
                         frame_validate(msg.data, msg.len,
                                        MASTER_FRAME_HEAD, FRAME_END))) {
                        cancelled = true;
                        break;
                    }
                }
                if (confirmed || cancelled) break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        if (cancelled) {
            sendStandbyAckGrace(master_mac, wake_session);
            _espnow.recvStop();
            resumeStandby();
            return;
        }
        if (!confirmed) {
            ESP_LOGW(MAIN_TAG, "Wake confirmation timed out; returning to BLE standby");
            _espnow.recvStop();
            resumeStandby();
            return;
        }
    }

    _radar.init();
    ESP_LOGI(MAIN_TAG, "State: WORK, source=%s", started_by_ble ? "BLE" : "button");

    const uint32_t work_started = millis();
    bool standby_requested = false;
    bool notify_end = false;
    bool remote_end = false;

    while (true) {
        if (millis() - work_started > WORK_TIMEOUT_MS) {
            ESP_LOGI(MAIN_TAG, "Work timeout");
            notify_end = true;
            break;
        }

        WakeupSource ignored_button = WAKEUP_POWER_ON;
        if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
            digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED ||
            _power.readButtonEvent(&ignored_button)) {
            ESP_LOGI(MAIN_TAG, "Outside button exits work mode");
            notify_end = true;
            break;
        }

        espnow_msg_t msg{};
        while (_espnow.read(&msg)) {
            if (matchingSessionFrame(msg, master_mac, MASTER_FRAME_HEAD,
                                     FRAME_STANDBY, wake_session)) {
                ESP_LOGI(MAIN_TAG, "Standby command received");
                standby_requested = true;
                break;
            }
            if (memcmp(msg.src_mac, master_mac, 6) == 0 &&
                frame_validate(msg.data, msg.len,
                               MASTER_FRAME_HEAD, FRAME_END)) {
                ESP_LOGI(MAIN_TAG, "Legacy END command received");
                remote_end = true;
                break;
            }
        }
        if (standby_requested || remote_end) break;

        _radar.loop();
        RadarData data{};
        if (_radar.getData(&data)) {
#if RADAR_DATA_DEBUG
            // CORE_DEBUG_LEVEL=2 suppresses noisy BLE library Info logs. Keep
            // the requested work-time radar trace independent of that level.
            Serial.printf("Radar: dist=%u cm, angle=%.2f deg\n",
                          data.dist_mm, data.angle_deg);
#endif
            protocol_frame_t frame{};
            // Keep the existing radar driver untouched. Its legacy field name
            // says mm, but the module value is physically cm and is forwarded
            // unchanged into the common protocol.
            frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA,
                        data.dist_mm, static_cast<int16_t>(data.angle_deg * 100));
            _espnow.send(master_mac,
                         reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
        }
        vTaskDelay(pdMS_TO_TICKS(RADAR_SEND_INTERVAL_MS));
    }

    _radar.shutdown();
    if (standby_requested) {
        sendStandbyAckGrace(master_mac, wake_session);
    } else if (notify_end || remote_end) {
        sendControlFrame(master_mac, FRAME_END, END_SEND_COUNT, wake_session);
    }

    _espnow.recvStop();
    resumeStandby();
}
