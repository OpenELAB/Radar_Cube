#include "peer_manager.h"

#include <WiFi.h>
#include <cstring>

#include "app_config.h"
#include "protocol.h"

namespace {
constexpr char NVS_NAMESPACE[] = "radar_pair";
constexpr char MASTER_MAC_KEY[] = "master_mac";
constexpr char MASTER_FLAG_KEY[] = "master_ok";
}

bool PeerManager::hasMaster()
{
    if (!_preferences.begin(NVS_NAMESPACE, true)) return false;
    const bool saved = _preferences.getBool(MASTER_FLAG_KEY, false);
    const size_t length = _preferences.getBytesLength(MASTER_MAC_KEY);
    _preferences.end();
    return saved && length == 6;
}

bool PeerManager::loadMaster(uint8_t master_mac[6])
{
    if (!master_mac || !_preferences.begin(NVS_NAMESPACE, true)) return false;
    const bool saved = _preferences.getBool(MASTER_FLAG_KEY, false);
    const size_t loaded = saved
        ? _preferences.getBytes(MASTER_MAC_KEY, master_mac, 6)
        : 0;
    _preferences.end();
    return loaded == 6;
}

bool PeerManager::saveMaster(const uint8_t master_mac[6])
{
    if (!master_mac || !_preferences.begin(NVS_NAMESPACE, false)) return false;
    const bool mac_saved =
        _preferences.putBytes(MASTER_MAC_KEY, master_mac, 6) == 6;
    const bool flag_saved =
        _preferences.putBool(MASTER_FLAG_KEY, mac_saved) == 1;
    _preferences.end();
    return mac_saved && flag_saved;
}

void PeerManager::clear()
{
    if (!_preferences.begin(NVS_NAMESPACE, false)) return;
    _preferences.remove(MASTER_MAC_KEY);
    _preferences.remove(MASTER_FLAG_KEY);
    _preferences.end();
}

void PeerManager::stopWireless()
{
    _espnow.recvStop();
    _espnow.deinit();
    if (WiFi.getMode() != WIFI_OFF) WiFi.mode(WIFI_OFF);
}

bool PeerManager::pair(uint32_t timeout_ms)
{
    if (hasMaster()) return true;
    if (timeout_ms == 0) return false;

    _espnow.init();
    _espnow.recvStart();

    bool paired = false;
    const uint32_t started_ms = millis();
    while (!paired && millis() - started_ms < timeout_ms) {
        espnow_msg_t message{};
        if (!_espnow.readBlocking(&message, pdMS_TO_TICKS(100))) continue;
        if (!frame_validate(message.data, message.len, MASTER_FRAME_HEAD,
                            FRAME_MASTER_MATCH)) {
            continue;
        }

        // Persist only after the live radio path can address the inside unit.
        // Otherwise a transient add-peer failure would leave a false pairing
        // record that survives the next reboot.
        if (!_espnow.addPeer(message.src_mac) || !saveMaster(message.src_mac)) {
            break;
        }

        protocol_frame_t response{};
        frame_build(&response, SLAVE_FRAME_HEAD, FRAME_SLAVE_MATCH);
        for (uint32_t attempt = 0; attempt < PAIR_ACK_REPEAT_COUNT; ++attempt) {
            _espnow.send(message.src_mac,
                         reinterpret_cast<const uint8_t*>(&response),
                         sizeof(response));
            if (attempt + 1 < PAIR_ACK_REPEAT_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(PAIR_ACK_GAP_MS));
            }
        }
        paired = true;
    }

    stopWireless();
    return paired;
}
