#include "mac_match.h"
#include "config.h"

// NVS 键名
static const char* NVS_NAMESPACE = "mac";
// static const char* NVS_KEY_FLAG  = "peer_saved";
// static const char* NVS_KEY_MAC   = "peer_mac";

// 修改键名，主机有两个从机键名，从机只有一个主机键名
// #ifdef INSIDE
//     static const char* SLAVE_A_FLAG = "slave_a_saved";
//     static const char* SLAVE_A_MAC  = "slave_a_mac";
//     static const char* SLAVE_B_FLAG = "slave_b_saved";
//     static const char* SLAVE_B_MAC  = "slave_b_mac";
//     static const char* SLAVE_NAMES[] = {"A", "B"};
// #endif

// #ifdef OUTSIDE
    static const char* MASTER_FLAG = "master_saved";
    static const char* MASTER_MAC  = "master_mac";
// #endif

// ======================== NVS 操作 ========================

// ====================== 主机NVS操作 ======================
// #ifdef INSIDE
//     // 判断从机A是否存在
//     bool MacMatch::has_slave_a_mac()
//     {
//         _prefs.begin(NVS_NAMESPACE, true);
//         bool saved = _prefs.getBool(SLAVE_A_FLAG, false);
//         _prefs.end();
//         return saved;
//     }
//     // 判断从机B是否存在
//     bool MacMatch::has_slave_b_mac()
//     {
//         _prefs.begin(NVS_NAMESPACE, true);
//         bool saved = _prefs.getBool(SLAVE_B_FLAG, false);
//         _prefs.end();
//         return saved;
//     }
//     // 读取从机的MAC地址
//     bool MacMatch::load_slave_mac(uint8_t mac_out[6], uint8_t slave_id)
//     {
//         _prefs.begin(NVS_NAMESPACE, true);
//         bool ok = false;
//         switch (slave_id) {
//             case SLAVE_A_ID:
//                 ok = (_prefs.getBytes(SLAVE_A_MAC, mac_out, 6) == 6);
//                 break;
//             case SLAVE_B_ID:
//                 ok = (_prefs.getBytes(SLAVE_B_MAC, mac_out, 6) == 6);
//                 break;
//         }
//         _prefs.end();
//         if (!ok) {
//             ESP_LOGE(MAC_TAG, "Failed to load slave MAC from NVS");
//         }
//         return ok;
//     }
//     // 保存从机的MAC地址
//     void MacMatch::save_slave_mac(const uint8_t mac[6], uint8_t slave_id)
//     {
//         _prefs.begin(NVS_NAMESPACE, false);
//         switch (slave_id) {
//             case SLAVE_A_ID:
//                 _prefs.putBytes(SLAVE_A_MAC, mac, 6);
//                 _prefs.putBool(SLAVE_A_FLAG, true);
//                 break;
//             case SLAVE_B_ID:
//                 _prefs.putBytes(SLAVE_B_MAC, mac, 6);
//                 _prefs.putBool(SLAVE_B_FLAG, true);
//                 break;
//         }
//         _prefs.end();
//         ESP_LOGI(MAC_TAG, "Slave %s MAC saved: %02X:%02X:%02X:%02X:%02X:%02X", 
//              SLAVE_NAMES[slave_id], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
//     }
//     void MacMatch::clear_slave_mac()
//     {
//         _prefs.begin(NVS_NAMESPACE, false);
//         _prefs.remove(SLAVE_A_MAC);
//         _prefs.remove(SLAVE_A_FLAG);
//         _prefs.remove(SLAVE_B_MAC);
//         _prefs.remove(SLAVE_B_FLAG);
//         _prefs.end();
//         ESP_LOGI(MAC_TAG, "Slave MAC cleared");
//     }

// #endif

// ====================== 从机NVS操作 ======================
// #ifdef OUTSIDE
    bool MacMatch::has_master_mac()
    {
        _prefs.begin(NVS_NAMESPACE, true);
        bool saved = _prefs.getBool(MASTER_FLAG, false);
        _prefs.end();
        return saved;
    }
    bool MacMatch::load_master_mac(uint8_t mac_out[6])
    {
        _prefs.begin(NVS_NAMESPACE, true);
        bool ok = (_prefs.getBytes(MASTER_MAC, mac_out, 6) == 6);
        _prefs.end();
        if (!ok) {
            ESP_LOGE(MAC_TAG, "Failed to load master MAC from NVS");
        }
        return ok;
    }
    void MacMatch::save_master_mac(const uint8_t mac[6])
    {
        _prefs.begin(NVS_NAMESPACE, false);
        _prefs.putBytes(MASTER_MAC, mac, 6);
        _prefs.putBool(MASTER_FLAG, true);
        _prefs.end();
        ESP_LOGI(MAC_TAG, "Master MAC saved: %02X:%02X:%02X:%02X:%02X:%02X"
             , mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    void MacMatch::clear_master_mac()
    {
        _prefs.begin(NVS_NAMESPACE, false);
        _prefs.remove(MASTER_MAC);
        _prefs.remove(MASTER_FLAG);
        _prefs.end();
        ESP_LOGI(MAC_TAG, "Master MAC cleared");
    }
// #endif

// ======================== 配对流程 ========================

bool MacMatch::pair(uint8_t max_retry)
{
    // 配对的前提应该是NVS里没有保存地址才能进行下一步，所以是先判断还是直接清除配对
    // 这里的逻辑我先改成的判断，如果NVS里有，直接跳过配对，返回false
// #ifdef INSIDE
//     bool has_slave_a = has_slave_a_mac();
//     bool has_slave_b = has_slave_b_mac();
//     if (has_slave_a && has_slave_b) {
//         ESP_LOGI(MAC_TAG, "Slave A and B MAC already saved, skipping pair");
//         return false;
//     }
//     else if(has_slave_a){
//         // 这种情况是从机A配对上了，但是从机B没有配对上，从NVS里读取处A的mac地址用于下面配对从机B
//         load_slave_mac(_slave_a_mac, SLAVE_A_ID);
//         ESP_LOGI(MAC_TAG, "Slave A MAC loaded: %02X:%02X:%02X:%02X:%02X:%02X"
//              , _slave_a_mac[0], _slave_a_mac[1], _slave_a_mac[2], _slave_a_mac[3], _slave_a_mac[4], _slave_a_mac[5]);
//     }
//     else if(has_slave_b){
//         // 这种情况应该可以省略，逻辑上应该不存在这种情况
//         load_slave_mac(_slave_b_mac, SLAVE_B_ID);
//         ESP_LOGI(MAC_TAG, "Slave B MAC loaded: %02X:%02X:%02X:%02X:%02X:%02X"
//              , _slave_b_mac[0], _slave_b_mac[1], _slave_b_mac[2], _slave_b_mac[3], _slave_b_mac[4], _slave_b_mac[5]);
//     }
// #endif

// #ifdef OUTSIDE
    if(has_master_mac()){
        ESP_LOGI(MAC_TAG, "Master MAC already saved, skipping pair");
        return false;
    }

// #endif

    _espnow.init();
    _espnow.recvStart();

    bool matched = false;
    espnow_msg_t msg;

// #ifdef INSIDE
//     // 主机：广播配对请求，等从机回复
//     _espnow.addPeer(ESPNOW_BROADCAST);

//     protocol_frame_t req;
//     frame_build(&req, MASTER_FRAME_HEAD, FRAME_MASTER_MATCH);

//     for (uint8_t i = 0; i < max_retry && !matched; i++) {
//         _espnow.send(ESPNOW_BROADCAST, (uint8_t*)&req, sizeof(req));
//         ESP_LOGI(MAC_TAG, "Pair request %d/%d", i + 1, max_retry);

//         for (int t = 0; t < PAIR_ROUND_CHECKS && !matched; t++) {
//             vTaskDelay(pdMS_TO_TICKS(PAIR_POLL_INTERVAL_MS));
//             if (_espnow.read(&msg)) {
//                 if (frame_validate(msg.data, msg.len, SLAVE_FRAME_HEAD, FRAME_SLAVE_MATCH)) {
//                     /* 先判断A槽是不是空的，如果是空的就视为从机A放入A槽保存mac地址
//                     如果A槽不空，判断一下B槽是不是空的，如果空的在判断一下收到的mac地址和A槽的
//                     是不是同一个去重，不是同一个就放入B槽视为从机B保存mac，结束配对流程 
//                     */
                    
//                     // 判断A槽是否为空
//                     if (!has_slave_a_mac()) {
//                         memcpy(_slave_a_mac, msg.src_mac, 6);
//                         save_slave_mac(_slave_a_mac, SLAVE_A_ID);
//                         has_slave_a = true;
//                         ESP_LOGI(MAC_TAG, "Slave A MAC saved: %02X:%02X:%02X:%02X:%02X:%02X"
//                              , _slave_a_mac[0], _slave_a_mac[1], _slave_a_mac[2], _slave_a_mac[3], _slave_a_mac[4], _slave_a_mac[5]);
//                     }
//                     // 判断B槽是否为空和是否与A槽地址一样
//                     else if (!has_slave_b_mac() && memcmp(_slave_a_mac, msg.src_mac, 6) != 0) {
//                         memcpy(_slave_b_mac, msg.src_mac, 6);
//                         save_slave_mac(_slave_b_mac, SLAVE_B_ID);
//                         has_slave_b = true;
//                         ESP_LOGI(MAC_TAG, "Slave B MAC saved: %02X:%02X:%02X:%02X:%02X:%02X"
//                              , _slave_b_mac[0], _slave_b_mac[1], _slave_b_mac[2], _slave_b_mac[3], _slave_b_mac[4], _slave_b_mac[5]);
//                     }
//                     if (has_slave_a && has_slave_b) {
//                         matched = true;
//                     }
//                 } else {
//                     ESP_LOGW(MAC_TAG, "Received unexpected frame (head/type mismatch), ignoring");
//                 }
//             }
//         }
//     }

//     _espnow.delPeer(ESPNOW_BROADCAST);
// #endif

// #ifdef OUTSIDE
    // 从机：等主机广播，收到后回复
    for (uint8_t i = 0; i < max_retry && !matched; i++) {
        vTaskDelay(pdMS_TO_TICKS(PAIR_POLL_INTERVAL_MS));
        if (_espnow.read(&msg)) {
            if (frame_validate(msg.data, msg.len, MASTER_FRAME_HEAD, FRAME_MASTER_MATCH)) {
                memcpy(_peer_mac, msg.src_mac, 6);
                save_master_mac(_peer_mac);

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
// #endif

    _espnow.recvStop();

// #ifdef INSIDE
//     if(matched)
//     {
//         ESP_LOGI(MAC_TAG, "Pair OK! Slave A :%02X:%02X:%02X:%02X:%02X:%02X, Slave B: %02X:%02X:%02X:%02X:%02X:%02X"
//              , _slave_a_mac[0], _slave_a_mac[1], _slave_a_mac[2], _slave_a_mac[3], _slave_a_mac[4], _slave_a_mac[5]
//              , _slave_b_mac[0], _slave_b_mac[1], _slave_b_mac[2], _slave_b_mac[3], _slave_b_mac[4], _slave_b_mac[5]);
//     }
//     else
//     {
//         ESP_LOGE(MAC_TAG, "Pair FAILED after %d retries", max_retry);
//     }
// #endif

// #ifdef OUTSIDE
    if (matched) {
        ESP_LOGI(MAC_TAG, "Pair OK! Peer: %02X:%02X:%02X:%02X:%02X:%02X",
                 _peer_mac[0], _peer_mac[1], _peer_mac[2],
                 _peer_mac[3], _peer_mac[4], _peer_mac[5]);
    }
    else {
        ESP_LOGE(MAC_TAG, "Pair FAILED after %d retries", max_retry);
    }
// #endif
    return matched;
}
