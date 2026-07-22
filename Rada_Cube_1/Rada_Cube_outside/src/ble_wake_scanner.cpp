#include "ble_wake_scanner.h"

#include <cstring>

#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

#include "ble_wake_protocol.h"
#include "config.h"

namespace {
constexpr uint32_t BLE_HOST_SYNC_TIMEOUT_MS = 120;
BleWakeScanner* scanner_instance = nullptr;
}

bool BleWakeScanner::begin()
{
    if (!_event_queue) _event_queue = xQueueCreate(1, sizeof(BleWakeEvent));
    return _event_queue != nullptr;
}

bool BleWakeScanner::start()
{
    if (_scanning) return true;
    if (!begin()) return false;

    // These two controller messages are configuration notices, not faults.
    // Suppressing them avoids spending wake time transmitting UART text.
    esp_log_level_set("BLE_INIT", ESP_LOG_ERROR);

    if (!_ble_initialized) {
        scanner_instance = this;
        _host_synced = false;

        const esp_err_t init_result = nimble_port_init();
        if (init_result != ESP_OK) {
            ESP_LOGE(BLE_WAKE_TAG, "NimBLE initialization failed: %d",
                     static_cast<int>(init_result));
            scanner_instance = nullptr;
            return false;
        }

        ble_hs_cfg.reset_cb = onHostReset;
        ble_hs_cfg.sync_cb = onHostSync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        nimble_port_freertos_init(hostTask);

        const uint32_t sync_started = millis();
        while (!_host_synced && millis() - sync_started < BLE_HOST_SYNC_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!_host_synced) {
            ESP_LOGE(BLE_WAKE_TAG, "NimBLE host synchronization timed out");
            nimble_port_stop();
            nimble_port_deinit();
            scanner_instance = nullptr;
            return false;
        }
        _ble_initialized = true;
    }

    clearEvents();

    ble_gap_disc_params scan_params{};
    scan_params.itvl = static_cast<uint16_t>(BLE_SCAN_INTERVAL_MS * 1000U / 625U);
    scan_params.window = static_cast<uint16_t>(BLE_SCAN_WINDOW_MS * 1000U / 625U);
    scan_params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    scan_params.limited = 0;
    scan_params.passive = 1;
    scan_params.filter_duplicates = 1;

    const int scan_result = ble_gap_disc(
        _own_address_type, BLE_HS_FOREVER, &scan_params, onGapEvent, this);
    _scanning = scan_result == 0;
    if (!_scanning) {
        ESP_LOGE(BLE_WAKE_TAG, "Passive scan failed: %d", scan_result);
    }
    ESP_LOGI(BLE_WAKE_TAG, "Passive scan %s", _scanning ? "started" : "failed");
    return _scanning;
}

void BleWakeScanner::stop()
{
    if (_scanning) {
        const int cancel_result = ble_gap_disc_cancel();
        if (cancel_result != 0 && cancel_result != BLE_HS_EALREADY) {
            ESP_LOGW(BLE_WAKE_TAG, "Passive scan cancel failed: %d", cancel_result);
        }
    }
    _scanning = false;

    if (_ble_initialized) {
        const int stop_result = nimble_port_stop();
        if (stop_result != 0) {
            ESP_LOGW(BLE_WAKE_TAG, "NimBLE host stop failed: %d", stop_result);
        }
        nimble_port_deinit();
        _ble_initialized = false;
        _host_synced = false;
        scanner_instance = nullptr;
    }
}

bool BleWakeScanner::scanBurst(const uint8_t expected_master[6],
                               uint32_t timeout_ms,
                               BleWakeEvent* result)
{
    if (!expected_master || !result || timeout_ms == 0) return false;

    memcpy(_expected_master_mac, expected_master, sizeof(_expected_master_mac));
    _filter_master = true;
    _last_session_id = 0;
    memset(_last_master_mac, 0, sizeof(_last_master_mac));

    if (!start()) {
        _filter_master = false;
        return false;
    }

    BleWakeEvent event{};
    const bool received = xQueueReceive(
        _event_queue, &event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    stop();
    _filter_master = false;
    if (received) *result = event;
    return received;
}

void BleWakeScanner::clearEvents()
{
    if (_event_queue) {
        BleWakeEvent discarded{};
        while (xQueueReceive(_event_queue, &discarded, 0) == pdTRUE) {}
    }
}

void BleWakeScanner::hostTask(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleWakeScanner::onHostReset(int reason)
{
    ESP_LOGW(BLE_WAKE_TAG, "NimBLE host reset: %d", reason);
}

void BleWakeScanner::onHostSync()
{
    if (!scanner_instance) return;

    uint8_t address_type = 0;
    const int result = ble_hs_id_infer_auto(0, &address_type);
    if (result != 0) {
        ESP_LOGE(BLE_WAKE_TAG, "Unable to determine BLE address type: %d", result);
        return;
    }
    scanner_instance->_own_address_type = address_type;
    scanner_instance->_host_synced = true;
}

int BleWakeScanner::onGapEvent(ble_gap_event* event, void* argument)
{
    auto* scanner = static_cast<BleWakeScanner*>(argument);
    if (!scanner || !event) return 0;

    if (event->type == BLE_GAP_EVENT_DISC) {
        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data) == 0 &&
            fields.mfg_data && fields.mfg_data_len > 0) {
            scanner->onAdvertisement(fields.mfg_data, fields.mfg_data_len,
                                     event->disc.rssi);
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        scanner->_scanning = false;
    }
    return 0;
}

void BleWakeScanner::onAdvertisement(const uint8_t* manufacturer_data,
                                     size_t manufacturer_data_length, int rssi)
{
    if (!manufacturer_data || !_event_queue) return;

    uint32_t session_id = 0;
    uint8_t master_mac[BLE_WAKE_MAC_LENGTH]{};
    if (!bleWakeValidate(manufacturer_data, manufacturer_data_length,
                         &session_id, master_mac)) return;
    if (_filter_master &&
        memcmp(master_mac, _expected_master_mac, BLE_WAKE_MAC_LENGTH) != 0) return;
    if (session_id == _last_session_id &&
        memcmp(master_mac, _last_master_mac, BLE_WAKE_MAC_LENGTH) == 0) return;

    _last_session_id = session_id;
    memcpy(_last_master_mac, master_mac, sizeof(_last_master_mac));
    BleWakeEvent event{};
    memcpy(event.master_mac, master_mac, sizeof(event.master_mac));
    event.session_id = session_id;
    event.rssi = rssi;
    ESP_LOGI(BLE_WAKE_TAG,
             "Wake advertisement received, master=%02X:%02X:%02X:%02X:%02X:%02X, session=%08lX, RSSI=%d",
             event.master_mac[0], event.master_mac[1], event.master_mac[2],
             event.master_mac[3], event.master_mac[4], event.master_mac[5],
             static_cast<unsigned long>(session_id), event.rssi);
    // Publish only after the callback has finished logging. Otherwise the main
    // task can tear down NimBLE/UART in the middle of this line.
    xQueueOverwrite(_event_queue, &event);
}
