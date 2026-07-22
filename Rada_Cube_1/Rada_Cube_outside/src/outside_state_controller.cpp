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
    PowerManager& power,
    EspNowManager& espnow,
    MacMatch& matcher,
    RadarModule& radar)
    : _power(power),
      _espnow(espnow),
      _matcher(matcher),
      _radar(radar)
{
}

void OutsideStateController::shutdownWireless()
{
    _espnow.deinit();
    if (WiFi.getMode() != WIFI_OFF) WiFi.mode(WIFI_OFF);
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
        return;
    }

    uint8_t master_mac[6]{};
    if (!_matcher.load_master_mac(master_mac)) {
        return;
    }

    _espnow.init();
    if (!_espnow.addPeer(master_mac)) {
        ESP_LOGE(MAIN_TAG, "Unable to add paired inside unit as ESP-NOW peer");
        shutdownWireless();
        return;
    }
    _espnow.recvStart();

    if (started_by_ble) {
        ESP_LOGI(MAIN_TAG, "Waiting for wake confirmation, session=%08lX",
                 static_cast<unsigned long>(wake_session));
        bool confirmed = false;
        bool standby_cancelled = false;
        bool end_cancelled = false;
        const uint32_t handshake_started = millis();

        while (!confirmed && !standby_cancelled && !end_cancelled &&
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
                                             FRAME_STANDBY, wake_session)) {
                        standby_cancelled = true;
                        break;
                    }
                    if (memcmp(msg.src_mac, master_mac, 6) == 0 &&
                        frame_validate(msg.data, msg.len,
                                       MASTER_FRAME_HEAD, FRAME_END)) {
                        end_cancelled = true;
                        break;
                    }
                }
                if (confirmed || standby_cancelled || end_cancelled) break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        if (standby_cancelled) {
            sendStandbyAckGrace(master_mac, wake_session);
            shutdownWireless();
            return;
        }
        if (end_cancelled) {
            sendControlFrame(master_mac, FRAME_END, END_SEND_COUNT, wake_session);
            shutdownWireless();
            return;
        }
        if (!confirmed) {
            ESP_LOGW(MAIN_TAG, "Wake confirmation timed out; returning to deep sleep");
            shutdownWireless();
            return;
        }
    }

    _radar.init();
    ESP_LOGI(MAIN_TAG, "State: WORK, source=%s", started_by_ble ? "BLE" : "button");

    const uint32_t work_started = millis();
    uint32_t last_radar_send = 0;
    bool standby_requested = false;
    bool notify_end = false;
    bool remote_end = false;

    while (true) {
        if (millis() - work_started > WORK_TIMEOUT_MS) {
            ESP_LOGI(MAIN_TAG, "Work timeout");
            notify_end = true;
            break;
        }

        if (digitalRead(USER_BUTTON_PIN) == BUTTON_PRESSED ||
            digitalRead(DEV_BUTTON_PIN) == BUTTON_PRESSED ||
            _power.buttonEventPending()) {
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
        if (millis() - last_radar_send >= RADAR_SEND_INTERVAL_MS &&
            _radar.getData(&data)) {
#if RADAR_DATA_DEBUG
            ESP_LOGI(RADAR_TAG, "dist=%u cm, angle=%.2f deg",
                     data.dist_cm, data.angle_deg);
#endif
            protocol_frame_t frame{};
            frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA,
                        data.dist_cm, static_cast<int16_t>(data.angle_deg * 100));
            _espnow.send(master_mac,
                         reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
            last_radar_send = millis();
        }
        // Poll buttons and control frames promptly while keeping radar reports
        // rate-limited by RADAR_SEND_INTERVAL_MS above.
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    _radar.shutdown();
    if (standby_requested) {
        sendStandbyAckGrace(master_mac, wake_session);
    } else if (notify_end || remote_end) {
        sendControlFrame(master_mac, FRAME_END, END_SEND_COUNT, wake_session);
    }

    shutdownWireless();
}
