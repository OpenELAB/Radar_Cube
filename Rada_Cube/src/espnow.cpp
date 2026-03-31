#include <Arduino.h>
#include "espnow.h"
#include "config.h"

// ======================== 常量 ========================

const uint8_t ESPNOW_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const char* TAG = "ESPNOW";

// ======================== 回调转发 ========================

EspNowManager* EspNowManager::_instance = nullptr;

void EspNowManager::_onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if (_instance) _instance->_onRecv(info, data, len);
}

void EspNowManager::_onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if (len <= 0 && !_rx_queue) return;
    // TODO: 可以考虑加个锁来保护 buffer，但目前主循环里读得足够慢，不存在竞争问题

    // 整理数据
    espnow_msg_t msg;
    memcpy(msg.src_mac, info->src_addr, 6);
    msg.len = (len > ESPNOW_MAX_DATA) ? ESPNOW_MAX_DATA : len;
    memcpy(msg.data, data, msg.len);

    // 放入队列
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendToBackFromISR(_rx_queue, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ======================== 初始化 / 反初始化 ========================

void EspNowManager::init()
{
    if (_inited) return;   // 幂等，重复调用不会出错

    // 创建队列
    if(_rx_queue == nullptr){
        _rx_queue = xQueueCreate(ESPNOW_QUEUE_DEPTH, sizeof(espnow_msg_t));
        if (_rx_queue == nullptr)
        {
            ESP_LOGI(TAG, "Queue create failed!");
            return;
        }
    }

    _instance = this;

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "init failed, restarting...");
        ESP.restart();
    }
    _inited = true;

    uint8_t mac[6];
    WiFi.macAddress(mac);
    ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void EspNowManager::deinit()
{
    if (!_inited) return;

    recvStop();
    esp_now_deinit();

    // 删除队列，释放内存
    if(_rx_queue)
    {
        vQueueDelete(_rx_queue);
        _rx_queue = nullptr;
    }

    _inited = false;
    ESP_LOGI(TAG, "deinited");
}

// ======================== 基础 API ========================

void EspNowManager::getMac(uint8_t mac_out[6])
{
    WiFi.macAddress(mac_out);
}

bool EspNowManager::addPeer(const uint8_t mac[6])
{
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_peer failed, err=%d", err);
    }
    return (err == ESP_OK);
}

bool EspNowManager::delPeer(const uint8_t mac[6])
{
    return (esp_now_del_peer(mac) == ESP_OK);
}

esp_err_t EspNowManager::send(const uint8_t* peer_mac, const uint8_t* data, size_t len)
{
    esp_err_t err = esp_now_send(peer_mac, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send failed, err=%d", err);
    }
    return err;
}

// ======================== 接收 API ========================

void EspNowManager::recvStart()
{
    if(!_rx_queue) return;

    // 清空旧数据
    xQueueReset(_rx_queue);
    esp_now_register_recv_cb(_onRecvStatic);
    ESP_LOGI(TAG, "rece started, queue depth=%d", ESPNOW_QUEUE_DEPTH);
}

bool EspNowManager::hasNewData()
{
    // 队列中存储的消息数量
    return uxQueueMessagesWaiting(_rx_queue) > 0;
}

// 非堵塞读取
bool EspNowManager::read(espnow_msg_t* msg_out)
{
    if (!_rx_queue || !msg_out) return false;

    // 非堵塞读取
    return xQueueReceive(_rx_queue, msg_out, 0) == pdTRUE;
}

// 堵塞读取
bool EspNowManager::readBlocking(espnow_msg_t* msg_out, TickType_t wait_ticks)
{
    if(!_rx_queue || !msg_out) return false;

    return xQueueReceive(_rx_queue, msg_out, wait_ticks) == pdTRUE;
}

// 获取队列中消息数量
UBaseType_t EspNowManager::getQueueCount()
{
    return uxQueueMessagesWaiting(_rx_queue);
}

void EspNowManager::recvStop()
{
    esp_now_unregister_recv_cb();
    ESP_LOGI(TAG, "recv stopped");
}
