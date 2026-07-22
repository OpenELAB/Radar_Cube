#include "ble_wake_protocol.h"

#include <cstring>

uint8_t bleWakeChecksum(const BleWakePayload& payload)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&payload);
    uint8_t checksum = 0;
    for (size_t i = 0; i < sizeof(BleWakePayload) - 1; ++i) checksum ^= bytes[i];
    return checksum;
}

void bleWakeBuild(BleWakePayload* payload,
                  const uint8_t master_mac[BLE_WAKE_MAC_LENGTH],
                  uint32_t session_id)
{
    if (!payload || !master_mac) return;
    payload->company_id = BLE_WAKE_COMPANY_ID;
    payload->magic = BLE_WAKE_MAGIC;
    payload->version = BLE_WAKE_VERSION;
    payload->command = static_cast<uint8_t>(BleWakeCommand::Wake);
    memcpy(payload->master_mac, master_mac, BLE_WAKE_MAC_LENGTH);
    payload->session_id = session_id;
    payload->checksum = bleWakeChecksum(*payload);
}

bool bleWakeValidate(const uint8_t* data, size_t length, uint32_t* session_id,
                     uint8_t master_mac[BLE_WAKE_MAC_LENGTH])
{
    if (!data || length != sizeof(BleWakePayload)) return false;

    BleWakePayload payload{};
    memcpy(&payload, data, sizeof(payload));
    if (payload.company_id != BLE_WAKE_COMPANY_ID ||
        payload.magic != BLE_WAKE_MAGIC ||
        payload.version != BLE_WAKE_VERSION ||
        payload.command != static_cast<uint8_t>(BleWakeCommand::Wake) ||
        payload.checksum != bleWakeChecksum(payload)) return false;

    if (session_id) *session_id = payload.session_id;
    if (master_mac) {
        memcpy(master_mac, payload.master_mac, BLE_WAKE_MAC_LENGTH);
    }
    return true;
}
