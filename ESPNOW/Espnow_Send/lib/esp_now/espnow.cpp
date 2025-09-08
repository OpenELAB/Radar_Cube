#include "espnow.h"

Preferences ESPNOW_Rada::prefs;

ESPNOW_Rada* ESPNOW_Rada::_instance = NULL;

ESPNOW_Rada::ESPNOW_Rada()
{
    _instance = this;
}


void ESPNOW_Rada::Rada_rece_init()
{
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) 
    {
        Serial.println("Error initializing ESP-NOW");
        ESP.restart();
    }
    String stored = readSavedMac();
    if(stored != "")
    {
        Serial.println("Buzzer mac exists");
        Serial.println(stored);
        sscanf(stored.c_str(),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        &_buzzer_mac[0], &_buzzer_mac[1], &_buzzer_mac[2], &_buzzer_mac[3], &_buzzer_mac[4], &_buzzer_mac[5]
        );
    }
    else
    {
        Serial.println("Buzzer mac does not exist");
        esp_now_register_recv_cb(_onRecv);
    }
}

String ESPNOW_Rada::readSavedMac()
{
    prefs.begin("Buzzer", true);
    bool ok = prefs.isKey("Buzzer_mac");
    prefs.end();
    if(ok)
    {
        prefs.begin("Buzzer", true);
        String mac = prefs.getString("Buzzer_mac");
        prefs.end();
        return mac;
    }
    else
    {
        return "";
    }
}

void ESPNOW_Rada::_onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{


    if(len != 6)
    {
        return;
    }
    const uint8_t *mac = info->src_addr;
    prefs.begin("Buzzer", true);
    bool exists = prefs.isKey("Buzzer_mac");
    prefs.end();

    if(exists) 
    {
        Serial.println("Buzzer mac exists");
        return;
    }
    prefs.begin("Buzzer", false);
    char mac_str[18];
    sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    prefs.putString("Buzzer_mac", mac_str);
    prefs.end();
    if(_instance) 
    {
        memcpy(_instance->_buzzer_mac, data, 6);
    }
    Serial.printf("Buzzer mac saved, MAC address: %s\n", mac_str);
    esp_now_unregister_recv_cb();

}

void ESPNOW_Rada::unicast_mode()
{
    Serial.println("Unicast Mode");
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, _buzzer_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    send_selfmac();
    esp_now_register_recv_cb(_recv_form_buzzer);
}

void ESPNOW_Rada::send_selfmac()
{
    if(_buzzer_mac[0] == 0 && _buzzer_mac[1] ==0)
    {
        Serial.println("No peer Mac yet");
        return;
    }
    uint8_t sely_mac[] = {0};
    WiFi.macAddress(sely_mac);
    esp_err_t result = esp_now_send(_buzzer_mac, sely_mac, 6);
    Serial.println(result == ESP_OK ? "Sent self mac" : "Failed to send self mac");
}

void ESPNOW_Rada::send_rada_data(uint8_t* data, size_t len)
{
    esp_now_send(_buzzer_mac, data, len);
}

void ESPNOW_Rada::_recv_form_buzzer(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    Serial.printf("Received data is %s\n", data);
}