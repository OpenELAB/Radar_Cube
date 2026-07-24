#include "wake_session_controller.h"

#include <WiFi.h>
#include <cstring>

#include "app_config.h"
#include "protocol.h"

namespace {
#if POWER_TEST_LOG_ENABLED
#define TEST_LOGI(format, ...) ESP_LOGI("XIAO_SESSION", format, ##__VA_ARGS__)
#define TEST_LOGW(format, ...) ESP_LOGW("XIAO_SESSION", format, ##__VA_ARGS__)
#else
#define TEST_LOGI(format, ...) do {} while (0)
#define TEST_LOGW(format, ...) do {} while (0)
#endif
}

bool WakeSessionController::isMatchingControl(
    const espnow_msg_t& message,
    const uint8_t master_mac[6],
    frame_type_t type,
    uint32_t session_id) const
{
    if (memcmp(message.src_mac, master_mac, 6) != 0 ||
        !frame_validate(message.data, message.len, MASTER_FRAME_HEAD, type)) {
        return false;
    }

    protocol_frame_t frame{};
    memcpy(&frame, message.data, sizeof(frame));
    return frame_get_session(&frame) == session_id;
}

void WakeSessionController::sendSessionFrame(
    const uint8_t master_mac[6],
    frame_type_t type,
    uint32_t session_id,
    uint8_t repeat_count)
{
    protocol_frame_t frame{};
    frame_build_session(&frame, SLAVE_FRAME_HEAD, type, session_id);
    for (uint8_t attempt = 0; attempt < repeat_count; ++attempt) {
        _espnow.send(master_mac, reinterpret_cast<const uint8_t*>(&frame),
                     sizeof(frame));
        if (attempt + 1 < repeat_count) vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void WakeSessionController::sendRadarSample(const uint8_t master_mac[6])
{
    protocol_frame_t frame{};
    frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA,
                SIMULATED_DISTANCE_CM, SIMULATED_ANGLE_CENTIDEG);
    _espnow.send(master_mac, reinterpret_cast<const uint8_t*>(&frame),
                 sizeof(frame));
}

void WakeSessionController::acknowledgeStandby(
    const uint8_t master_mac[6], uint32_t session_id)
{
    const uint32_t started_ms = millis();
    uint32_t last_ack_ms = 0;
    while (millis() - started_ms < STANDBY_GRACE_TEST_MS) {
        const uint32_t now_ms = millis();
        if (last_ack_ms == 0 || now_ms - last_ack_ms >= 150) {
            sendSessionFrame(master_mac, FRAME_STANDBY_ACK, session_id, 2);
            last_ack_ms = millis();
        }

        espnow_msg_t message{};
        while (_espnow.read(&message)) {
            if (isMatchingControl(message, master_mac, FRAME_STANDBY,
                                  session_id)) {
                sendSessionFrame(master_mac, FRAME_STANDBY_ACK,
                                 session_id, 2);
                last_ack_ms = millis();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WakeSessionController::stopWireless()
{
    _espnow.recvStop();
    _espnow.deinit();
    if (WiFi.getMode() != WIFI_OFF) WiFi.mode(WIFI_OFF);
}

WakeSessionResult WakeSessionController::run(
    const BleWakeEvent& wake_event,
    const uint8_t master_mac[6])
{
    if (!master_mac ||
        memcmp(wake_event.master_mac, master_mac, 6) != 0 ||
        wake_event.session_id == 0) {
        return WakeSessionResult::WirelessError;
    }

    _espnow.init();
    if (!_espnow.addPeer(master_mac)) {
        stopWireless();
        return WakeSessionResult::WirelessError;
    }
    _espnow.recvStart();

    const uint32_t session_id = wake_event.session_id;
    const uint32_t handshake_started_ms = millis();
    uint32_t last_ack_ms = 0;
    bool confirmed = false;

    while (!confirmed &&
           millis() - handshake_started_ms < WAKE_CONFIRM_TIMEOUT_TEST_MS) {
        const uint32_t now_ms = millis();
        if (last_ack_ms == 0 || now_ms - last_ack_ms >= WAKE_ACK_PERIOD_MS) {
            sendSessionFrame(master_mac, FRAME_WAKE_ACK, session_id, 2);
            last_ack_ms = millis();
        }

        espnow_msg_t message{};
        if (!_espnow.readBlocking(&message, pdMS_TO_TICKS(20))) continue;
        if (isMatchingControl(message, master_mac, FRAME_WAKE_CONFIRM,
                              session_id)) {
            confirmed = true;
        } else if (isMatchingControl(message, master_mac, FRAME_STANDBY,
                                     session_id)) {
            acknowledgeStandby(master_mac, session_id);
            stopWireless();
            return WakeSessionResult::Cancelled;
        } else if (isMatchingControl(message, master_mac, FRAME_END,
                                     session_id)) {
            sendSessionFrame(master_mac, FRAME_END, session_id, 3);
            stopWireless();
            return WakeSessionResult::Cancelled;
        }
    }

    if (!confirmed) {
        TEST_LOGW("Wake confirm timeout, session=%08lX",
                  static_cast<unsigned long>(session_id));
        stopWireless();
        return WakeSessionResult::ConfirmTimeout;
    }

    TEST_LOGI("Wake confirmed, session=%08lX, RSSI=%d",
              static_cast<unsigned long>(session_id), wake_event.rssi);

    const uint32_t work_started_ms = millis();
    uint32_t last_sample_ms = 0;
    while (millis() - work_started_ms < SIMULATED_WORK_MS) {
        const uint32_t now_ms = millis();
        if (last_sample_ms == 0 ||
            now_ms - last_sample_ms >= SIMULATED_RADAR_INTERVAL_MS) {
            sendRadarSample(master_mac);
            last_sample_ms = millis();
        }

        espnow_msg_t message{};
        while (_espnow.read(&message)) {
            if (isMatchingControl(message, master_mac, FRAME_STANDBY,
                                  session_id)) {
                acknowledgeStandby(master_mac, session_id);
                stopWireless();
                return WakeSessionResult::Completed;
            }
            if (isMatchingControl(message, master_mac, FRAME_END,
                                  session_id)) {
                sendSessionFrame(master_mac, FRAME_END, session_id, 3);
                stopWireless();
                return WakeSessionResult::Completed;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // End the radar-free test deterministically. The inside unit will then send
    // its normal standby command to the other node as well.
    sendSessionFrame(master_mac, FRAME_END, session_id, 3);
    stopWireless();
    return WakeSessionResult::Completed;
}
