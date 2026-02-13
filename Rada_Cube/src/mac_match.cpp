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

#endif

#ifdef OUTSIDE
    const char* MASTER_MAC_ADDR = "master_mac_addr";

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
        ESP_LOGI(MAC_TAG, "MAC_FLAG key not exist, create and write false");
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
void MacMatch::write_mac_addr(const uint8_t* mac_addr)
{
    mac_prefe.begin("MAC_SPACE", false);
#ifdef INSIDE
    mac_prefe.putBytes(SLAVE_MAC_ADDR, mac_addr, 6);
#endif

#ifdef OUTSIDE
    mac_prefe.putBytes(MASTER_MAC_ADDR, mac_addr, 6);

#endif

    mac_prefe.end();
}

// 获取存在mac地址
bool MacMatch::read_mac_addr(uint8_t* mac_buf, size_t buf_len)
{
    if(buf_len != 6) return false;
    mac_prefe.begin("MAC_SPACE", true);

    size_t len = 0;
#ifdef INSIDE
    len = mac_prefe.getBytes(SLAVE_MAC_ADDR, mac_buf, buf_len);
#endif

#ifdef OUTSIDE
    len = mac_prefe.getBytes(MASTER_MAC_ADDR, mac_buf, buf_len);
#endif
    mac_prefe.end();
    return (len == 6);
}

void MacMatch::clear_mac_addr()
{
    mac_prefe.begin("MAC_SPACE", false);
#ifdef INSIDE
    mac_prefe.remove(SLAVE_MAC_ADDR);
#endif

#ifdef OUTSIDE
    mac_prefe.remove(MASTER_MAC_ADDR);
#endif

    mac_prefe.end();
}


void MacMatch::mac_init()
{
    WiFi.mode(WIFI_STA);
    if(esp_now_init() != ESP_OK)
    {
        ESP_LOGI(MAC_TAG, "ESP-NOW Init Failed");
        // 重启系统
        ESP.restart();
    } 
    // 读取自己的mac地址
    WiFi.macAddress(_self_mac);
    ESP_LOGI(MAC_TAG, "self mac address: %02x:%02x:%02x:%02x:%02x:%02x", _self_mac[0], _self_mac[1], _self_mac[2], _self_mac[3], _self_mac[4], _self_mac[5]);
}


#ifdef INSIDE

// 车内模块广播模式
void MacMatch::broadcast_init()
{
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
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

    // 主机mac匹配通信协议帧
    protocol_frame_t master_mac_match_frame;
    master_mac_match_frame.head = MASTER_FRAME_HEAD;
    master_mac_match_frame.type = MASTER_MATCH_FRAME;
    master_mac_match_frame.len  = FRAME_DATA_LEN;
    // mac地址匹配帧的数据部分全为0
    master_mac_match_frame.dist = MAC_MATCH_DATA;
    master_mac_match_frame.angle = MAC_MATCH_DATA;
    master_mac_match_frame.reserve = RESERVE_DATA;
    // CRC校验值
    master_mac_match_frame.crc = crc_set(&master_mac_match_frame);


    while(!slave_mac_flag && broad_send_count < 20)
    {
        broad_send_count++;
        // 发送数据
        esp_now_send(BROADCAST_ADDR, (uint8_t*)&master_mac_match_frame, sizeof(master_mac_match_frame));
        uint8_t *p = (uint8_t*)&master_mac_match_frame;
        ESP_LOGI(MAC_TAG, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], 
                p[7], p[8], p[9], p[10], p[11], p[12], p[13]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 注销回调函数
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();
}

void MacMatch::broadcast_onsent(const esp_now_send_info_t* info, esp_now_send_status_t status)
{
    getInstance().onBroadcastSent(info, status);
}

void MacMatch::broadcast_onrece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    getInstance().onBroadcastRece(info, data, len);
}

// 广播模式发送回调函数
void MacMatch::onBroadcastSent(const esp_now_send_info_t* info, esp_now_send_status_t status)

{
    ESP_LOGI(MAC_TAG, "%s", (status == ESP_NOW_SEND_SUCCESS? "Send OK" : "Send Fail"));
}
// 广播模式接收回调函数
void  MacMatch::onBroadcastRece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if(len == sizeof(protocol_frame_t))
    {
        protocol_frame_t *p = (protocol_frame_t*)data;
        if(p->head == SLAVE_FRAME_HEAD && p->type == SLAVE_MATCH_FRAME)
        {
            // 从机的返回帧
            if(p->crc == crc_set(p))
            {
                uint8_t* raw = (uint8_t*)p;
                // 打印接收到的数据
                ESP_LOGI(MAC_TAG, "RX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6],
                        raw[7], raw[8], raw[9], raw[10], raw[11], raw[12], raw[13]);

                // 打印mac地址
                ESP_LOGI(MAC_TAG, "%02X:%02X:%02X:%02X:%02X:%02X", info->src_addr[0], info->src_addr[1], info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5]);

                // 写入地址
                write_mac_addr(info->src_addr);
                memcpy(_slave_mac, info->src_addr, 6);
                write_mac_exist_flag(SLAVE_MAC_ADDR_EXIST);
                slave_mac_flag = SLAVE_MAC_ADDR_EXIST;
                ESP_LOGI(MAC_TAG, "Slave mac address match success!");
                // 删除广播地址
                esp_now_del_peer(BROADCAST_ADDR);
            }
        }
    }
}


#endif


// 车外模块
#ifdef OUTSIDE

void MacMatch::slave_onsent(const esp_now_send_info_t* info, esp_now_send_status_t status)
{
    getInstance().onSlaveSent(info, status);
}
void MacMatch::slave_onrece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    getInstance().onSlaveRece(info, data, len);
}

void MacMatch:: onSlaveSent(const esp_now_send_info_t* info, esp_now_send_status_t status)
{
    ESP_LOGI(MAC_TAG, "%s", (status == ESP_NOW_SEND_SUCCESS? "Send OK" : "Send Fail"));
}

void MacMatch:: onSlaveRece(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
   if(len == sizeof(protocol_frame_t))
   {
       protocol_frame_t *p = (protocol_frame_t*)data;
       if(p->head == MASTER_FRAME_HEAD && p->type == MASTER_MATCH_FRAME)
       {
           // mac地址匹配帧
           if(p->crc == crc_set(p))
           {
                // 表示mac地址已匹配上了
                uint8_t* raw = (uint8_t*)p;
                // 打印接收到的数据
                ESP_LOGI(MAC_TAG, "RX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], 
                        raw[7], raw[8], raw[9], raw[10], raw[11], raw[12], raw[13]);

                // 打印mac地址
                ESP_LOGI(MAC_TAG, "%02X:%02X:%02X:%02X:%02X:%02X", info->src_addr[0], info->src_addr[1], info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5]);
                
                // 写入mac地址
                write_mac_addr(info->src_addr);
                memcpy(_master_mac, info->src_addr, 6);
                // 写入主机地址已存在标志位
                write_mac_exist_flag(MASTER_MAC_ADDR_EXIST);
                // 设置主机地址已存在标志位
                master_mac_flag = MASTER_MAC_ADDR_EXIST;
                
           }
        }
   }
}


#endif


bool MacMatch::mac_exist()
{
    if(!key_exist_flag()) return false;
    if(!get_mac_exist_flag()) return false;
    return true;
}




// 对mac地址匹配的总逻辑
bool MacMatch::mac_match()
{

    // 初始化ESP-NOW，保存自己的mac地址
    mac_init();
    // 判断NVS mac空间里是否存在key
    key_exist_flag();

    // 判断mac地址是否存在的标志位在执行对应的操作
    // =========================== mac 地址存在处理 ============================
    if(get_mac_exist_flag())
    {

#ifdef INSIDE
        // 从机的地址已存在
        slave_mac_flag = SLAVE_MAC_ADDR_EXIST;
        read_mac_addr(_slave_mac, sizeof(_slave_mac));

#endif

#ifdef OUTSIDE
        // 主机的地址已存在
        master_mac_flag = MASTER_MAC_ADDR_EXIST;
        read_mac_addr(_master_mac, sizeof(_master_mac));
#endif

        // mac地址已存在
        uint8_t mac[6];
        if(read_mac_addr(mac, sizeof(mac)))
        {
            ESP_LOGI(MAC_TAG, "mac address exist: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); 
        } 
    }
    // ============================ mac 地址不存在处理 ============================
    else
    {
        ESP_LOGI(MAC_TAG, "mac address not exist");
    
#ifdef INSIDE
        slave_mac_flag = false;
        broadcast_init();
        broadcast_send();
#endif

#ifdef OUTSIDE
        uint8_t config_count = 0;
        master_mac_flag = MASTER_MAC_ADDR_NOT_EXIST;
        esp_now_register_recv_cb(slave_onrece);
        while(master_mac_flag == MASTER_MAC_ADDR_NOT_EXIST && config_count < 20)
        {
            config_count++;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        if(config_count == 20)
        {
            ESP_LOGI(MAC_TAG, "MAC config Time out");
            return false;
        }
        else
        {
            ESP_LOGI(MAC_TAG, "MAC config success");
        }

        // 从机接收到主机MAC地址后, 配置为单播模式去返回配对成功的数据帧
        // 从机mac匹配通信协议帧
        protocol_frame_t slave_mac_match_frame;
        slave_mac_match_frame.head = SLAVE_FRAME_HEAD;
        slave_mac_match_frame.type = SLAVE_MATCH_FRAME;
        slave_mac_match_frame.len  = FRAME_DATA_LEN;
        // mac地址匹配帧的数据部分全为0
        slave_mac_match_frame.dist = MAC_MATCH_DATA;
        slave_mac_match_frame.angle = MAC_MATCH_DATA;
        slave_mac_match_frame.reserve = RESERVE_DATA;
        // CRC校验值
        slave_mac_match_frame.crc = crc_set(&slave_mac_match_frame);

        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof(peer));
        memcpy(peer.peer_addr, _master_mac, sizeof(_master_mac));
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);

        // 发送数据
        esp_now_send(_master_mac, (uint8_t*)&slave_mac_match_frame, sizeof(slave_mac_match_frame));
        uint8_t *p = (uint8_t*)&slave_mac_match_frame;
        
        // ESP_LOGI(MAC_TAG, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
        //         p[0], p[1], p[2], p[3], p[4], p[5], p[6], 
        //         p[7], p[8], p[9], p[10], p[11], p[12], p[13]);

#endif
    }
    return true;

}

void MacMatch::get_mac_addr()
{
#ifdef INSIDE
    read_mac_addr(_slave_mac, sizeof(_slave_mac));
#endif

#ifdef OUTSIDE
    read_mac_addr(_master_mac, sizeof(_master_mac));
#endif
}
