#include "ble_standby_scanner.h"

#include <cstring>

#include <esp_err.h>
#include <esp_log.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

#include "ble_wake_protocol.h"

namespace {
constexpr char TAG[] = "BLE_STANDBY";
constexpr EventBits_t HOST_SYNCED_BIT = BIT0;
constexpr EventBits_t SCAN_STOPPED_BIT = BIT1;
constexpr TickType_t HOST_SYNC_TIMEOUT = pdMS_TO_TICKS(2000);
constexpr TickType_t SCAN_STOP_TIMEOUT = pdMS_TO_TICKS(250);
constexpr uint32_t BLE_SCAN_UNIT_US = 625;
constexpr uint16_t BLE_SCAN_UNITS_MIN = 0x0004;
constexpr uint16_t BLE_SCAN_UNITS_MAX = 0x4000;

// NimBLE's host callbacks do not carry an application argument. This scanner is
// therefore deliberately a single-instance service.
BleStandbyScanner* scanner_instance = nullptr;

bool millisecondsToScanUnits(uint32_t milliseconds, uint16_t* units)
{
    if (!units || milliseconds == 0) return false;

    const uint64_t microseconds = static_cast<uint64_t>(milliseconds) * 1000U;
    const uint64_t rounded_units =
        (microseconds + BLE_SCAN_UNIT_US - 1U) / BLE_SCAN_UNIT_US;
    if (rounded_units < BLE_SCAN_UNITS_MIN ||
        rounded_units > BLE_SCAN_UNITS_MAX) {
        return false;
    }

    *units = static_cast<uint16_t>(rounded_units);
    return true;
}
}  // namespace

bool BleStandbyScanner::begin(const uint8_t expected_master[6])
{
    if (!expected_master) return false;

    // Changing the accepted peer while callbacks may be running would race the
    // host task. Configure the peer once, before the resident scanner starts.
    if (_configured) {
        return memcmp(_expected_master_mac, expected_master,
                      sizeof(_expected_master_mac)) == 0;
    }

    _event_queue = xQueueCreate(1, sizeof(BleWakeEvent));
    _host_events = xEventGroupCreate();
    if (!_event_queue || !_host_events) {
        if (_event_queue) {
            vQueueDelete(_event_queue);
            _event_queue = nullptr;
        }
        if (_host_events) {
            vEventGroupDelete(_host_events);
            _host_events = nullptr;
        }
        ESP_LOGE(TAG, "Unable to allocate scanner synchronization objects");
        return false;
    }

    memcpy(_expected_master_mac, expected_master,
           sizeof(_expected_master_mac));
    _configured = true;
    return true;
}

bool BleStandbyScanner::start(uint32_t interval_ms, uint32_t window_ms)
{
    if (!_configured || window_ms > interval_ms) return false;

    uint16_t interval_units = 0;
    uint16_t window_units = 0;
    if (!millisecondsToScanUnits(interval_ms, &interval_units) ||
        !millisecondsToScanUnits(window_ms, &window_units) ||
        window_units > interval_units) {
        ESP_LOGE(TAG, "Invalid scan timing: interval=%lums, window=%lums",
                 static_cast<unsigned long>(interval_ms),
                 static_cast<unsigned long>(window_ms));
        return false;
    }

    if (_scanning.load()) {
        return interval_units == _scan_interval_units &&
               window_units == _scan_window_units;
    }

    _scan_interval_units = interval_units;
    _scan_window_units = window_units;

    if (!initializeBle()) return false;
    return startConfiguredScan();
}

bool BleStandbyScanner::waitForWake(BleWakeEvent* event,
                                    TickType_t timeout_ticks)
{
    if (!event || !_event_queue) return false;
    return xQueueReceive(_event_queue, event, timeout_ticks) == pdTRUE;
}

void BleStandbyScanner::pauseScan()
{
    // Stop publishing before cancelling the controller procedure. A discovery
    // report already queued by NimBLE after this point is safely ignored.
    _accept_events.store(false);

    if (!_scanning.exchange(false)) return;

    const int result = ble_gap_disc_cancel();
    if (result != 0 && result != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "Passive scan cancel failed: %d", result);
    }
    if (result == 0 && _host_events) {
        // Wait until NimBLE has delivered DISC_COMPLETE. This prevents an
        // immediate resume from racing the still-active discovery procedure.
        xEventGroupWaitBits(_host_events, SCAN_STOPPED_BIT, pdFALSE, pdTRUE,
                            SCAN_STOP_TIMEOUT);
    }
}

bool BleStandbyScanner::resumeScan()
{
    if (!_ble_initialized) return false;
    if (_scanning.load()) return true;
    return startConfiguredScan();
}

void BleStandbyScanner::shutdown()
{
    _accept_events.store(false);
    pauseScan();

    if (!_ble_initialized) return;

    const int result = nimble_port_stop();
    if (result != 0) {
        ESP_LOGW(TAG, "NimBLE host stop failed: %d", result);
    }
    nimble_port_deinit();

    _ble_initialized = false;
    _scanning.store(false);
    if (_host_events) {
        xEventGroupClearBits(_host_events,
                             HOST_SYNCED_BIT | SCAN_STOPPED_BIT);
    }
    if (scanner_instance == this) scanner_instance = nullptr;
}

void BleStandbyScanner::clearEvents()
{
    if (!_event_queue) return;

    BleWakeEvent discarded{};
    while (xQueueReceive(_event_queue, &discarded, 0) == pdTRUE) {}
}

bool BleStandbyScanner::initializeBle()
{
    if (_ble_initialized) return true;
    if (!_host_events) return false;
    if (scanner_instance && scanner_instance != this) {
        ESP_LOGE(TAG, "Only one BleStandbyScanner instance is supported");
        return false;
    }

    scanner_instance = this;
    xEventGroupClearBits(_host_events, HOST_SYNCED_BIT);

    const esp_err_t result = nimble_port_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE initialization failed: %d",
                 static_cast<int>(result));
        scanner_instance = nullptr;
        return false;
    }

    ble_hs_cfg.reset_cb = onHostReset;
    ble_hs_cfg.sync_cb = onHostSync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    nimble_port_freertos_init(hostTask);

    const EventBits_t bits = xEventGroupWaitBits(
        _host_events, HOST_SYNCED_BIT, pdFALSE, pdTRUE, HOST_SYNC_TIMEOUT);
    if ((bits & HOST_SYNCED_BIT) == 0) {
        ESP_LOGE(TAG, "NimBLE host synchronization timed out");
        nimble_port_stop();
        nimble_port_deinit();
        scanner_instance = nullptr;
        return false;
    }

    _ble_initialized = true;
    return true;
}

bool BleStandbyScanner::startConfiguredScan()
{
    if (!_ble_initialized || _scan_interval_units == 0 ||
        _scan_window_units == 0) {
        return false;
    }

    clearEvents();
    xEventGroupClearBits(_host_events, SCAN_STOPPED_BIT);

    ble_gap_disc_params parameters{};
    parameters.itvl = _scan_interval_units;
    parameters.window = _scan_window_units;
    parameters.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    parameters.limited = 0;
    parameters.passive = 1;
    parameters.filter_duplicates = 1;

    // Set this before starting the asynchronous procedure so an immediate
    // discovery report cannot be lost between ble_gap_disc() and its return.
    _accept_events.store(true);
    const int result = ble_gap_disc(_own_address_type, BLE_HS_FOREVER,
                                    &parameters, onGapEvent, this);
    if (result != 0) {
        _accept_events.store(false);
        _scanning.store(false);
        ESP_LOGE(TAG, "Passive scan start failed: %d", result);
        return false;
    }

    _scanning.store(true);
    ESP_LOGI(TAG, "Passive scan started: interval=%u, window=%u units",
             _scan_interval_units, _scan_window_units);
    return true;
}

void BleStandbyScanner::hostTask(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleStandbyScanner::onHostReset(int reason)
{
    BleStandbyScanner* scanner = scanner_instance;
    if (scanner) {
        scanner->_accept_events.store(false);
        scanner->_scanning.store(false);
        if (scanner->_host_events) {
            xEventGroupClearBits(scanner->_host_events, HOST_SYNCED_BIT);
            xEventGroupSetBits(scanner->_host_events, SCAN_STOPPED_BIT);
        }
    }
    ESP_LOGW(TAG, "NimBLE host reset: %d", reason);
}

void BleStandbyScanner::onHostSync()
{
    BleStandbyScanner* scanner = scanner_instance;
    if (!scanner) return;

    uint8_t address_type = 0;
    const int result = ble_hs_id_infer_auto(0, &address_type);
    if (result != 0) {
        ESP_LOGE(TAG, "Unable to determine BLE address type: %d", result);
        return;
    }

    scanner->_own_address_type = address_type;
    xEventGroupSetBits(scanner->_host_events, HOST_SYNCED_BIT);
}

int BleStandbyScanner::onGapEvent(ble_gap_event* event, void* argument)
{
    auto* scanner = static_cast<BleStandbyScanner*>(argument);
    if (!scanner || !event) return 0;

    if (event->type == BLE_GAP_EVENT_DISC) {
        if (!scanner->_accept_events.load()) return 0;

        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data) == 0 &&
            fields.mfg_data && fields.mfg_data_len > 0) {
            scanner->onAdvertisement(fields.mfg_data, fields.mfg_data_len,
                                     event->disc.rssi);
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        scanner->_accept_events.store(false);
        scanner->_scanning.store(false);
        if (scanner->_host_events) {
            xEventGroupSetBits(scanner->_host_events, SCAN_STOPPED_BIT);
        }
    }
    return 0;
}

void BleStandbyScanner::onAdvertisement(const uint8_t* manufacturer_data,
                                        size_t manufacturer_data_length,
                                        int rssi)
{
    if (!_accept_events.load() || !manufacturer_data || !_event_queue) return;

    uint32_t session_id = 0;
    uint8_t master_mac[BLE_WAKE_MAC_LENGTH]{};
    if (!bleWakeValidate(manufacturer_data, manufacturer_data_length,
                         &session_id, master_mac)) {
        return;
    }
    if (memcmp(master_mac, _expected_master_mac, BLE_WAKE_MAC_LENGTH) != 0) {
        return;
    }
    if (_has_last_session && session_id == _last_session_id &&
        memcmp(master_mac, _last_master_mac, BLE_WAKE_MAC_LENGTH) == 0) {
        return;
    }

    _has_last_session = true;
    _last_session_id = session_id;
    memcpy(_last_master_mac, master_mac, sizeof(_last_master_mac));

    BleWakeEvent event{};
    memcpy(event.master_mac, master_mac, sizeof(event.master_mac));
    event.session_id = session_id;
    event.rssi = rssi;

    // NimBLE invokes this from its host task, not an ISR. Queue overwrite keeps
    // the callback non-blocking while preserving the newest valid session.
    xQueueOverwrite(_event_queue, &event);
}
