#include "mac_match.h"
#include "config.h"

// NVS key name
static const char* NVS_NAMESPACE = "mac";
// static const char* NVS_KEY_FLAG  = "peer_saved";
// static const char* NVS_KEY_MAC   = "peer_mac";

// Modify the key name. The master has two slave key names, and the slave has only one master key name.
    static const char* SLAVE_A_FLAG = "slave_a_saved";
    static const char* SLAVE_A_MAC  = "slave_a_mac";
    static const char* SLAVE_B_FLAG = "slave_b_saved";
    static const char* SLAVE_B_MAC  = "slave_b_mac";
    static const char* SLAVE_NAMES[] = {"A", "B"};


// ======================== NVS operations ========================

// ====================== Host NVS operation ======================
    // Determine whether slave A exists
    bool MacMatch::has_slave_a_mac()
    {
        _prefs.begin(NVS_NAMESPACE, true);
        bool saved = _prefs.getBool(SLAVE_A_FLAG, false);
        _prefs.end();
        return saved;
    }
    // Determine whether slave B exists
    bool MacMatch::has_slave_b_mac()
    {
        _prefs.begin(NVS_NAMESPACE, true);
        bool saved = _prefs.getBool(SLAVE_B_FLAG, false);
        _prefs.end();
        return saved;
    }
    // Read the MAC address of the slave
    bool MacMatch::load_slave_mac(uint8_t mac_out[6], uint8_t slave_id)
    {
        _prefs.begin(NVS_NAMESPACE, true);
        bool ok = false;
        switch (slave_id) {
            case SLAVE_A_ID:
                ok = (_prefs.getBytes(SLAVE_A_MAC, mac_out, 6) == 6);
                break;
            case SLAVE_B_ID:
                ok = (_prefs.getBytes(SLAVE_B_MAC, mac_out, 6) == 6);
                break;
        }
        _prefs.end();
        if (!ok) {
            ESP_LOGE(MAC_TAG, "Failed to load slave MAC from NVS");
        }
        return ok;
    }
    // Save the MAC address of the slave machine
    void MacMatch::save_slave_mac(const uint8_t mac[6], uint8_t slave_id)
    {
        _prefs.begin(NVS_NAMESPACE, false);
        switch (slave_id) {
            case SLAVE_A_ID:
                _prefs.putBytes(SLAVE_A_MAC, mac, 6);
                _prefs.putBool(SLAVE_A_FLAG, true);
                break;
            case SLAVE_B_ID:
                _prefs.putBytes(SLAVE_B_MAC, mac, 6);
                _prefs.putBool(SLAVE_B_FLAG, true);
                break;
        }
        _prefs.end();
        ESP_LOGI(MAC_TAG, "Slave %s MAC saved: %02X:%02X:%02X:%02X:%02X:%02X", 
             SLAVE_NAMES[slave_id], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    void MacMatch::clear_slave_mac()
    {
        _prefs.begin(NVS_NAMESPACE, false);
        _prefs.remove(SLAVE_A_MAC);
        _prefs.remove(SLAVE_A_FLAG);
        _prefs.remove(SLAVE_B_MAC);
        _prefs.remove(SLAVE_B_FLAG);
        _prefs.end();
        ESP_LOGI(MAC_TAG, "Slave MAC cleared");
    }

// ======================== Matching process ========================

bool MacMatch::pair(uint8_t max_retry,
                    MacMatch::PairEventCallback callback,
                    void* callback_context)
{
    // The prerequisite for pairing is that there is no saved address in NVS before proceeding to the next step, so whether to judge first or clear the pairing directly
    // I will first change the logic here to judgment. If there is one in NVS, skip the pairing directly and return false.
    bool has_slave_a = has_slave_a_mac();
    bool has_slave_b = has_slave_b_mac();
    if (has_slave_a && has_slave_b) {
        ESP_LOGI(MAC_TAG, "Slave A and B MAC already saved, skipping pair");
        return false;
    }
    else if(has_slave_a){
        // In this case, slave A is paired, but slave B is not. The mac address of A is read from NVS and used to pair slave B below.
        load_slave_mac(_slave_a_mac, SLAVE_A_ID);
        ESP_LOGI(MAC_TAG, "Slave A MAC loaded: %02X:%02X:%02X:%02X:%02X:%02X"
             , _slave_a_mac[0], _slave_a_mac[1], _slave_a_mac[2], _slave_a_mac[3], _slave_a_mac[4], _slave_a_mac[5]);
    }
    else if(has_slave_b){
        // This situation should be omitted, and logically this situation should not exist
        load_slave_mac(_slave_b_mac, SLAVE_B_ID);
        ESP_LOGI(MAC_TAG, "Slave B MAC loaded: %02X:%02X:%02X:%02X:%02X:%02X"
             , _slave_b_mac[0], _slave_b_mac[1], _slave_b_mac[2], _slave_b_mac[3], _slave_b_mac[4], _slave_b_mac[5]);
    }

    _espnow.init();
    _espnow.recvStart();

    bool matched = false;
    espnow_msg_t msg;

    // Host: Broadcast pairing request and wait for slave reply
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
                    /* First determine whether slot A is empty. If it is empty, it is regarded as slave A and placed in slot A to save the mac address.
                    If slot A is occupied, check whether slot B is empty. Compare the received MAC
                    with slot A to avoid duplicates. If different, save it as slave B and finish pairing.
                    */
                    
                    // Determine whether slot A is empty
                    if (!has_slave_a_mac()) {
                        memcpy(_slave_a_mac, msg.src_mac, 6);
                        save_slave_mac(_slave_a_mac, SLAVE_A_ID);
                        if (callback != nullptr) {
                            callback(SLAVE_A_ID, _slave_a_mac, callback_context);
                        }
                        has_slave_a = true;
                        ESP_LOGI(MAC_TAG, "Slave A MAC saved: %02X:%02X:%02X:%02X:%02X:%02X"
                             , _slave_a_mac[0], _slave_a_mac[1], _slave_a_mac[2], _slave_a_mac[3], _slave_a_mac[4], _slave_a_mac[5]);
                    }
                    // Determine whether slot B is empty and has the same address as slot A
                    else if (!has_slave_b_mac() && memcmp(_slave_a_mac, msg.src_mac, 6) != 0) {
                        memcpy(_slave_b_mac, msg.src_mac, 6);
                        save_slave_mac(_slave_b_mac, SLAVE_B_ID);
                        if (callback != nullptr) {
                            callback(SLAVE_B_ID, _slave_b_mac, callback_context);
                        }
                        has_slave_b = true;
                        ESP_LOGI(MAC_TAG, "Slave B MAC saved: %02X:%02X:%02X:%02X:%02X:%02X"
                             , _slave_b_mac[0], _slave_b_mac[1], _slave_b_mac[2], _slave_b_mac[3], _slave_b_mac[4], _slave_b_mac[5]);
                    }
                    if (has_slave_a && has_slave_b) {
                        matched = true;
                    }
                } else {
                    ESP_LOGW(MAC_TAG, "Received unexpected frame (head/type mismatch), ignoring");
                }
            }
        }
    }

    _espnow.delPeer(ESPNOW_BROADCAST);
    _espnow.recvStop();

    if(matched)
    {
        ESP_LOGI(MAC_TAG, "Pair OK! Slave A :%02X:%02X:%02X:%02X:%02X:%02X, Slave B: %02X:%02X:%02X:%02X:%02X:%02X"
             , _slave_a_mac[0], _slave_a_mac[1], _slave_a_mac[2], _slave_a_mac[3], _slave_a_mac[4], _slave_a_mac[5]
             , _slave_b_mac[0], _slave_b_mac[1], _slave_b_mac[2], _slave_b_mac[3], _slave_b_mac[4], _slave_b_mac[5]);
    }
    else
    {
        ESP_LOGE(MAC_TAG, "Pair FAILED after %d retries", max_retry);
    }
    return matched;
}
