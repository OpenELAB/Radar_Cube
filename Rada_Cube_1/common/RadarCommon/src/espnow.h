#ifndef __ESPNOW_H__
#define __ESPNOW_H__

#include <esp_now.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ======================== Constant ========================
extern const uint8_t ESPNOW_BROADCAST[6];

// Maximum load of received messages
#define ESPNOW_MAX_DATA     32

// ======================== Queue ========================
#define ESPNOW_QUEUE_DEPTH        16

// ======================== Receive message structure ========================
typedef struct {
    uint8_t  src_mac[6];            // Sender MAC
    uint8_t  data[ESPNOW_MAX_DATA]; // Load
    uint8_t  len;                   // Actual load length
} espnow_msg_t;

/**
 * ESP-NOW communication management (pure communication layer, excluding business logic)
 *
 * Receiving principle:
 *   Callback → write the latest frame into the buffer + set the flag
 *   Main loop → Check flag bit → Read data → Clear flag bit
 *   Only the latest frame is retained, the old one is automatically overwritten (just right for radar scenes)
 *
 * Usage example:
 *
 *   EspNowManager Espnow;
 *
 *   // 1. Initialization (idempotent, repeated calls will be automatically skipped)
 *   Espnow.init();
 *
 *   // 2. Add the other party’s address + send
 *   Espnow.addPeer(peer_mac);
 *   Espnow.send(peer_mac, data, len);
 *
 *   // 3. Receive
 *   Espnow.recvStart();
 *   espnow_msg_t msg;
 *   if (Espnow.read(&msg)) {
 *       // Process msg.data, msg.len, msg.src_mac
 *   }
 *   Espnow.recvStop();
 *
 *   // 4. Deinitialization
 *   Espnow.deinit();
 */
class EspNowManager
{
public:

    // Default constructor
    EspNowManager() = default;

    // Copying prohibited
    EspNowManager(const EspNowManager&) = delete;
    EspNowManager& operator=(const EspNowManager&) = delete;

    // Initialize WiFi STA + ESP-NOW (idempotent, repeated calls will be automatically skipped)
    void init();

    // Deinitialization ESP-NOW
    void deinit();

    // Get the local MAC
    void getMac(uint8_t mac_out[6]);

    // Add/delete peer
    bool addPeer(const uint8_t mac[6]);
    bool delPeer(const uint8_t mac[6]);

    // Send data
    esp_err_t send(const uint8_t* peer_mac, const uint8_t* data, size_t len);

    // Enable reception (register callback)
    void recvStart();

    // Is there any new data arriving?
    bool hasNewData();

    // Non-blocking read
    bool read(espnow_msg_t* msg_out);

    // Blocked reading
    bool readBlocking(espnow_msg_t* msg_out, TickType_t wait_ticks);

    // Stop receiving (logout callback)
    void recvStop();

    // Get the current backlog of the queue
    UBaseType_t getQueueCount();

private:
    bool _inited = false;

    // ---- Queue ----
    QueueHandle_t    _rx_queue = nullptr;

    // ---- Serial port callback forwarding ----
    static EspNowManager* _instance;
    static void _onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    void _onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len);
};

#endif
