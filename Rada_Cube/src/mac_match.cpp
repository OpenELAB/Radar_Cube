#include "mac_match.h"
#include "config.h"

// NVS 键名
static const char* NVS_NAMESPACE = "mac";
static const char* NVS_KEY_FLAG  = "peer_saved";
static const char* NVS_KEY_MAC   = "peer_mac";

// ======================== NVS 操作 ========================

bool MacMatch::hasPeerMac()
{
    _prefs.begin(NVS_NAMESPACE, true);
    bool saved = _prefs.getBool(NVS_KEY_FLAG, false);
    _prefs.end();
    return saved;
}

bool MacMatch::loadPeerMac(uint8_t mac_out[6])
{
    _prefs.begin(NVS_NAMESPACE, true);
    bool ok = (_prefs.getBytes(NVS_KEY_MAC, mac_out, 6) == 6);
    _prefs.end();
    if (!ok) {
        ESP_LOGE(MAC_TAG, "Failed to load peer MAC from NVS");
    }
    return ok;
}

void MacMatch::savePeerMac(const uint8_t mac[6])
{
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putBytes(NVS_KEY_MAC, mac, 6);
    _prefs.putBool(NVS_KEY_FLAG, true);
    _prefs.end();
    ESP_LOGI(MAC_TAG, "Peer MAC saved: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void MacMatch::clearPeerMac()
{
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(NVS_KEY_MAC);
    _prefs.remove(NVS_KEY_FLAG);
    _prefs.end();
    ESP_LOGI(MAC_TAG, "Peer MAC cleared");
}

// ======================== 配对流程 ========================

bool MacMatch::pair(uint8_t max_retry)
{
    _espnow.init();
    _espnow.recvStart();

    bool matched = false;
    espnow_msg_t msg;

#ifdef INSIDE
    // 主机：广播配对请求，等从机回复
    _espnow.addPeer(ESPNOW_BROADCAST);

    protocol_frame_t req;
    frame_build(&req, MASTER_FRAME_HEAD, FRAME_MASTER_MATCH);

    for (uint8_t i = 0; i < max_retry && !matched; i++) {
        _espnow.send(ESPNOW_BROADCAST, (uint8_t*)&req, sizeof(req));
        ESP_LOGI(MAC_TAG, "Pair request %d/%d", i + 1, max_retry);

        for (int t = 0; t < PAIR_ROUND_CHECKS && !matched; t++) {
            vTaskDelay(pdMS_TO_TICKS(PAIR_POLL_INTERVAL_MS));
            if (_espnow.read(&msg)) {
                if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_SLAVE_MATCH)) {
                    memcpy(_peer_mac, msg.src_mac, 6);
                    savePeerMac(_peer_mac);
                    matched = true;
                } else {
                    ESP_LOGW(MAC_TAG, "Received unexpected frame (head/type mismatch), ignoring");
                }
            }
        }
    }

    _espnow.delPeer(ESPNOW_BROADCAST);
#endif

#ifdef OUTSIDE
    // 从机：等主机广播，收到后回复
    for (uint8_t i = 0; i < max_retry && !matched; i++) {
        vTaskDelay(pdMS_TO_TICKS(PAIR_POLL_INTERVAL_MS));
        if (_espnow.read(&msg)) {
            if (frame_validate(msg.data, msg.len, MASTER_FRAME_HEAD, FRAME_MASTER_MATCH)) {
                memcpy(_peer_mac, msg.src_mac, 6);
                savePeerMac(_peer_mac);

                // 回复配对应答
                _espnow.addPeer(_peer_mac);
                protocol_frame_t ack;
                frame_build(&ack, SLAVE_FRAME_HEAD, FRAME_SLAVE_MATCH);
                esp_err_t err = _espnow.send(_peer_mac, (uint8_t*)&ack, sizeof(ack));
                if (err != ESP_OK) {
                    ESP_LOGE(MAC_TAG, "Failed to send pair ACK, err=%d", err);
                } else {
                    ESP_LOGI(MAC_TAG, "Pair ACK sent");
                }
                matched = true;
            } else {
                ESP_LOGW(MAC_TAG, "Received unexpected frame, ignoring");
            }
        }
    }
#endif

    _espnow.recvStop();

    if (matched) {
        ESP_LOGI(MAC_TAG, "Pair OK! Peer: %02X:%02X:%02X:%02X:%02X:%02X",
                 _peer_mac[0], _peer_mac[1], _peer_mac[2],
                 _peer_mac[3], _peer_mac[4], _peer_mac[5]);
    } else {
        ESP_LOGE(MAC_TAG, "Pair FAILED after %d retries", max_retry);
    }
    return matched;
}
