#include "ble_wake_broadcaster.h"

#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <esp_bt.h>
#include <esp_system.h>

#include "ble_wake_protocol.h"
#include "config.h"

namespace {
constexpr uint32_t BLE_HOST_SYNC_TIMEOUT_MS = 120;
BleWakeBroadcaster* broadcaster_instance = nullptr;
}

bool BleWakeBroadcaster::start(const uint8_t master_mac[6])
{
    if (_running) return true;
    if (!master_mac) {
        ESP_LOGE(BLE_WAKE_TAG, "Master MAC is required for wake advertising");
        return false;
    }

    if (!_ble_initialized) {
        esp_log_level_set("BLE_INIT", ESP_LOG_ERROR);
        broadcaster_instance = this;
        _host_synced = false;

        const esp_err_t init_result = nimble_port_init();
        if (init_result != ESP_OK) {
            ESP_LOGE(BLE_WAKE_TAG, "NimBLE initialization failed: %d",
                     static_cast<int>(init_result));
            broadcaster_instance = nullptr;
            return false;
        }

        ble_hs_cfg.reset_cb = onHostReset;
        ble_hs_cfg.sync_cb = onHostSync;
        nimble_port_freertos_init(hostTask);

        const uint32_t sync_started = millis();
        while (!_host_synced && millis() - sync_started < BLE_HOST_SYNC_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!_host_synced) {
            ESP_LOGE(BLE_WAKE_TAG, "NimBLE host synchronization timed out");
            nimble_port_stop();
            nimble_port_deinit();
            broadcaster_instance = nullptr;
            return false;
        }

        const esp_err_t tx_power_result =
            esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20);
        if (tx_power_result != ESP_OK) {
            ESP_LOGE(BLE_WAKE_TAG, "Unable to set BLE advertising TX power to +20 dBm: %d",
                     static_cast<int>(tx_power_result));
            nimble_port_stop();
            nimble_port_deinit();
            broadcaster_instance = nullptr;
            return false;
        }

        const esp_power_level_t actual_tx_power =
            esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV);
        if (actual_tx_power != ESP_PWR_LVL_P20) {
            ESP_LOGW(BLE_WAKE_TAG,
                     "BLE advertising TX power readback mismatch: requested=%d, actual=%d",
                     static_cast<int>(ESP_PWR_LVL_P20),
                     static_cast<int>(actual_tx_power));
        } else {
            ESP_LOGI(BLE_WAKE_TAG, "BLE advertising TX power set to +20 dBm");
        }
        _ble_initialized = true;
    }

    _session_id = esp_random();
    if (_session_id == 0) _session_id = 1;

    BleWakePayload payload{};
    bleWakeBuild(&payload, master_mac, _session_id);

    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = reinterpret_cast<const uint8_t*>(&payload);
    fields.mfg_data_len = sizeof(payload);

    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) {
        ESP_LOGE(BLE_WAKE_TAG, "Unable to set wake advertisement data: %d", result);
        return false;
    }

    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_NON;
    params.itvl_min = BLE_WAKE_ADV_INTERVAL_MIN;
    params.itvl_max = BLE_WAKE_ADV_INTERVAL_MAX;

    result = ble_gap_adv_start(_own_address_type, nullptr, BLE_HS_FOREVER,
                               &params, onGapEvent, this);
    _running = result == 0;
    if (!_running) {
        ESP_LOGE(BLE_WAKE_TAG, "Unable to start wake advertising: %d", result);
    }
    ESP_LOGI(BLE_WAKE_TAG,
             "Wake advertising %s, master=%02X:%02X:%02X:%02X:%02X:%02X, session=%08lX",
             _running ? "started" : "failed",
             master_mac[0], master_mac[1], master_mac[2], master_mac[3],
             master_mac[4], master_mac[5],
             static_cast<unsigned long>(_session_id));
    return _running;
}

void BleWakeBroadcaster::stop()
{
    if (_running) {
        const int stop_result = ble_gap_adv_stop();
        if (stop_result != 0 && stop_result != BLE_HS_EALREADY) {
            ESP_LOGW(BLE_WAKE_TAG, "Wake advertising stop failed: %d", stop_result);
        }
    }
    _running = false;

    if (_ble_initialized) {
        const int stop_result = nimble_port_stop();
        if (stop_result != 0) {
            ESP_LOGW(BLE_WAKE_TAG, "NimBLE host stop failed: %d", stop_result);
        }
        nimble_port_deinit();
        _ble_initialized = false;
        _host_synced = false;
        broadcaster_instance = nullptr;
    }
    ESP_LOGI(BLE_WAKE_TAG, "Wake advertising stopped");
}

void BleWakeBroadcaster::hostTask(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleWakeBroadcaster::onHostReset(int reason)
{
    ESP_LOGW(BLE_WAKE_TAG, "NimBLE host reset: %d", reason);
}

void BleWakeBroadcaster::onHostSync()
{
    if (!broadcaster_instance) return;

    uint8_t address_type = 0;
    const int result = ble_hs_id_infer_auto(0, &address_type);
    if (result != 0) {
        ESP_LOGE(BLE_WAKE_TAG, "Unable to determine BLE address type: %d", result);
        return;
    }
    broadcaster_instance->_own_address_type = address_type;
    broadcaster_instance->_host_synced = true;
}

int BleWakeBroadcaster::onGapEvent(ble_gap_event* event, void* argument)
{
    auto* broadcaster = static_cast<BleWakeBroadcaster*>(argument);
    if (!broadcaster || !event) return 0;

    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        broadcaster->_running = false;
    }
    return 0;
}
