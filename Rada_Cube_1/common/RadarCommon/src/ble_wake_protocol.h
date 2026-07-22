#ifndef BLE_WAKE_PROTOCOL_H
#define BLE_WAKE_PROTOCOL_H

#include <Arduino.h>

// Manufacturer data is visible in passive scan reports; no connection is used.
constexpr uint16_t BLE_WAKE_COMPANY_ID = 0xFFFF;
constexpr uint16_t BLE_WAKE_MAGIC = 0x5243;
constexpr uint8_t BLE_WAKE_VERSION = 2;
constexpr size_t BLE_WAKE_MAC_LENGTH = 6;

enum class BleWakeCommand : uint8_t { Wake = 0x01 };

struct __attribute__((packed)) BleWakePayload {
    uint16_t company_id;
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    uint8_t master_mac[BLE_WAKE_MAC_LENGTH];
    uint32_t session_id;
    uint8_t checksum;
};

static_assert(sizeof(BleWakePayload) == 17, "BleWakePayload size changed");

uint8_t bleWakeChecksum(const BleWakePayload& payload);
void bleWakeBuild(BleWakePayload* payload,
                  const uint8_t master_mac[BLE_WAKE_MAC_LENGTH],
                  uint32_t session_id);
bool bleWakeValidate(const uint8_t* data, size_t length,
                     uint32_t* session_id = nullptr,
                     uint8_t master_mac[BLE_WAKE_MAC_LENGTH] = nullptr);

#endif
