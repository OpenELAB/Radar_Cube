#include "espnow.h"

Preferences ESPNOW_Buzzer::prefs;
ESPNOW_Buzzer* ESPNOW_Buzzer::_instance = NULL;

uint16_t ESPNOW_Buzzer::distBuf[MEDIAN_WIN] = {0};
uint8_t ESPNOW_Buzzer::bufIdx = 0;

ESPNOW_Buzzer::ESPNOW_Buzzer()
{
    _instance = this;
}

void ESPNOW_Buzzer::broadcast_init()
{
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        ESP.restart();
    }
    WiFi.macAddress(_selfmac);

    Serial.println("A:MAC = ");
    for (uint8_t b : _selfmac)
    {
        Serial.printf("%02x:", b);
    }
    Serial.println();
    String stored = readSavedmac();
    if (stored != "")
    {
        Serial.println("Rada mac exists and is: ");
        Serial.println(stored);
        sscanf(stored.c_str(),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        &_Rada_mac[0], &_Rada_mac[1], &_Rada_mac[2], &_Rada_mac[3], &_Rada_mac[4], &_Rada_mac[5]
        );
    }
    else
    {
        memcpy(_peer.peer_addr, BROADCAST_ADDR, 6);
        _peer.channel = 0;
        _peer.encrypt = false;
        esp_now_add_peer(&_peer);

        esp_now_register_recv_cb(_onRecv);
        esp_now_register_send_cb(_onSent);
    }
}

String ESPNOW_Buzzer::readSavedmac()
{
    prefs.begin("Rada", true);
    bool ok = prefs.isKey("Rada_mac");
    prefs.end();
    if (ok)
    {
        prefs.begin("Rada", true);
        String stored = prefs.getString("Rada_mac", "");
        prefs.end();
        return stored;
    }
    else
    {
        return "";
    }
}

void ESPNOW_Buzzer::broadcast_sent()
{

    esp_now_send(BROADCAST_ADDR, _selfmac, 6);

}

void ESPNOW_Buzzer::broadcast_loop()
{
    while(true)
    {
        prefs.begin("Rada", true);
        bool ok = prefs.isKey("Rada_mac");
        prefs.end();
        if(ok)
        {
            break;
        }
        broadcast_sent();
        delay(2000);
    }
}

void ESPNOW_Buzzer::_onSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Send OK" : "Send Fail");
}

void ESPNOW_Buzzer::_onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    Serial.println("Received data");
    Serial.printf("%s\n", data);
    if(len == 6 && !(prefs.isKey("Rada_mac")))
    {
        Serial.println("rada mac received");
        const uint8_t *mac = info->src_addr;
        prefs.begin("Rada", false);
        char mac_str[18];
        sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        prefs.putString("Rada_mac", mac_str);
        prefs.end();

        Serial.println("Saved mac address");
        if(_instance) 
        {
            memcpy(_instance->_Rada_mac, data, 6);
        }
        if(_instance) 
        {
            esp_now_del_peer(BROADCAST_ADDR);
            esp_now_unregister_recv_cb();
            esp_now_unregister_send_cb();
        }
    }
    else if(len != 0)
    {

    }
}

void ESPNOW_Buzzer::send_to_rada(uint8_t* data, size_t len)
{
    esp_now_send(_Rada_mac, data, len);
}

void ESPNOW_Buzzer::unicast_mode()
{
    memcpy(_peer.peer_addr, _Rada_mac, 6);
    _peer.channel = 0;
    _peer.encrypt = false;
    esp_now_add_peer(&_peer);

    esp_now_register_recv_cb(_Recv_rada);
    Serial.println("unicast mode");
}

void ESPNOW_Buzzer::_Recv_rada(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len == sizeof(uint16_t)) 
    {
        uint16_t raw;
        memcpy(&raw, data, sizeof(uint16_t));
        if(raw < 20 || raw > 600) return;
        distBuf[bufIdx] = raw;
        bufIdx = (bufIdx + 1) % MEDIAN_WIN;
    } 
    else 
    {
        Serial.println("Invalid data length");
    }
}

uint16_t ESPNOW_Buzzer::getMedianDistance()
{
    if (bufIdx == 0) return 0;
    uint8_t valid = (bufIdx < MEDIAN_WIN) ? bufIdx : MEDIAN_WIN;
    return median_of(distBuf, valid);
}

uint16_t ESPNOW_Buzzer::median_of(uint16_t* arr, uint8_t n)
{
    uint16_t tmp[n];
    memcpy(tmp, arr, n * sizeof(uint16_t));
    std::sort(tmp, tmp + n);
    return tmp[n / 2];
}
