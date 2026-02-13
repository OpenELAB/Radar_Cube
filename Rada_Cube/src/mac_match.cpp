#include <esp_now.h>
#include <WiFi.h>
#include "mac_match.h"
#include "pins.h"
#include "config.h"

// NVS里的命名空间
const char* MAC_SPACE = "mac";
// mac是否存在的标志位
const char* MAC_FLAG = "mac_flag";

// 对主机来说需要两个从机的mac地址，第一板样品主机只带一个从机验证，后续修改
#ifdef INSIDE
    const char* SLAVE_MAC_ADDR = "slave_mac_addr";
    // 广播地址
    const uint8_t BROADCAST_ADDR[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    // 广播模式发送的数据, 主机发送master，从机发送slave
    const char* BROADCAST_SEND_DATA = "master";

#endif

#ifdef OUTSIDE
    const char* MASTER_MAC_ADDR = "master_mac_addr";
    const char* BROADCAST_RECV_DATA = "salve";
#endif



// 判断mac空间里有没有mac存在的标志位
bool MacMatch::key_exist_flag()
{
    // 打开或创建一个空间, MAC_SPACE
    mac_prefe.begin("MAC_SPACE", false);
    bool key_exist = mac_prefe.isKey(MAC_FLAG);
    if(!key_exist)
    {
        // MAC_FLAG key不存在，写入false默认值，表示mac地址不存在
        ESP_LOGI(MAC_TAG, "MAC_FLAG key not exist, create and write false\r\n");
        mac_prefe.putBool(MAC_FLAG, false);
        mac_prefe.end();
        return false;
    }
    return true;
}
// 获取mac存在的标志位
bool MacMatch::get_mac_exist_flag()
{
    mac_prefe.begin("MAC_SPACE", true);
    bool mac_exist = mac_prefe.getBool(MAC_FLAG);
    mac_prefe.end();
    return mac_exist;
}

// 写入mac存在的标志位
void MacMatch::write_mac_exist_flag(bool flag)
{
    mac_prefe.begin("MAC_SPACE", false);
    mac_prefe.putBool(MAC_FLAG, flag);
    mac_prefe.end();
}

// 直接删除mac空间里的key和数据
bool MacMatch::clear_mac_exist_flag()
{
    mac_prefe.begin("MAC_SPACE", false);
    bool result = mac_prefe.remove(MAC_FLAG);
    mac_prefe.end();
    return result;
}

// 写入mac地址
void MacMatch::write_mac_addr(char* mac_addr)
{
    mac_prefe.begin("MAC_SPACE", false);
    mac_prefe.putString(SLAVE_MAC_ADDR, mac_addr);
    mac_prefe.end();
}

void MacMatch::mac_match_init()
{
    WiFi.mode(WIFI_STA);
    if(esp_now_init() != ESP_OK)
    {
        ESP_LOGI(MAC_TAG, "ESP-NOW Init Failed\r\n");
        // 重启系统
        ESP.restart();
    } 
    // 读取自己的mac地址
    WiFi.macAddress(_self_mac);
    ESP_LOGI(MAC_TAG, "self mac address: %02x:%02x:%02x:%02x:%02x:%02x\r\n", _self_mac[0], _self_mac[1], _self_mac[2], _self_mac[3], _self_mac[4], _self_mac[5]);
}


// 车内模块广播模式
void MacMatch::broadcast_init()
{
    esp_now_peer_info_t peer;
    memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // 注册广播模式的发送和接收信号
    esp_now_register_send_cb(broadcast_onsent);
    esp_now_register_recv_cb(broadcast_onrece);

}

// 广播模式循环发送的数据
void MacMatch::broadcast_send()
{
    uint8_t broad_send_count = 0;
    while(!slave1_mac_flag && broad_send_count < 10)
    {
        esp_now_send(BROADCAST_ADDR, (const uint8_t*)BROADCAST_SEND_DATA, strlen(BROADCAST_SEND_DATA) + 1);
        broad_send_count++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 广播模式发送回调函数
void MacMatch::broadcast_onsent(const esp_now_send_info_t* info, esp_now_send_status_t status)

{
    ESP_LOGI(MAC_TAG, "%s", (status == ESP_NOW_SEND_SUCCESS? "Send OK" : "Send Fail"));
}
// 广播模式接收回调函数
void  MacMatch::broadcast_onrece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{

}


// 对mac地址匹配的总逻辑
void MacMatch::mac_match()
{

    // 初始化ESP-NOW，保存自己的mac地址
    mac_match_init();

    // 判断NVS mac空间里是否存在key
    key_exist_flag();
    
    // 判断mac地址是否存在的标志位在执行对应的操作
    if(get_mac_exist_flag())
    {
        // 从机1的地址已存在
        slave1_mac_flag = true;
        // mac地址已存在
        ESP_LOGI(MAC_TAG, "mac address exist \r\n");
    }
    else
    {
        // mac地址不存在，车内模块去广播自己的地址，车外模块接收任何信号，（这里看一下需不需要约定好信号帧, 目前只发送了字符, 发送端发送master）
        ESP_LOGI(MAC_TAG, "mac address not exist \r\n");
        slave1_mac_flag = false;
        broadcast_init();
        broadcast_send();
        
    }
}
