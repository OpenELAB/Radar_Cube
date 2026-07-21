#include <Arduino.h>
#include <HardwareSerial.h>
#include "lora.h"
#include "protocol.h"

// NVS key name
static const char* NVS_NAMESPACE = "lora";
static const char* NVS_KEY_FLAG  = "configured";

// Lora module initialization AT command sequence
static const char* LORA_INIT_CMDS[] = {
    "AT+MODE=0",
    "AT+RFCH=18",
    "AT+PID=255",
    "AT+MAMP=2",
    "AT+MLPWR=2",
    "AT+MID=17",
    "AT+MODE=1"
};
static const int LORA_INIT_CMD_COUNT = sizeof(LORA_INIT_CMDS) / sizeof(LORA_INIT_CMDS[0]);

// In-car module Lora sleep command
static const char* LORA_SLEEP_CMDS[] = {
    "AT+MODE=0",
    "AT+MAMP=0",
    "AT+LPWR=1",
    "AT+MODE=1"
};
static const int LORA_SLEEP_CMD_COUNT = sizeof(LORA_SLEEP_CMDS) / sizeof(LORA_SLEEP_CMDS[0]);

// In-vehicle module Lora wake-up command
static const char* LORA_WAKEUP_CMDS[] = {
    "AT+MODE=0",
    "AT+MAMP=2",
    "AT+LPWR=0",
    "AT+MODE=1"
};
static const int LORA_WAKEUP_CMD_COUNT = sizeof(LORA_WAKEUP_CMDS) / sizeof(LORA_WAKEUP_CMDS[0]);


// ======================== NVS operations ========================

bool LoraManager::isConfigured()
{
    _prefs.begin(NVS_NAMESPACE, true);          // Read only
    bool flag = _prefs.getBool(NVS_KEY_FLAG, false);
    _prefs.end();
    return flag;
}

void LoraManager::setConfigured(bool flag)
{
    _prefs.begin(NVS_NAMESPACE, false);         // Reading and Writing
    _prefs.putBool(NVS_KEY_FLAG, flag);
    _prefs.end();
}

void LoraManager::clearConfigFlag()
{
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(NVS_KEY_FLAG);
    _prefs.end();
}

// ======================== Hardware initialization ========================

void LoraManager::init()
{
    LoraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    pinMode(LORA_CE_PIN, OUTPUT);
    pinMode(LORA_POWER_PIN, OUTPUT);
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
    // TODO: The power of the Lora module must be turned on here, otherwise the Lora configuration will fail. I don’t know the specific reason.
    digitalWrite(LORA_POWER_PIN, LORA_POWER_ON);
}

// ======================== AT command ========================

bool LoraManager::sendAT(const char* cmd, int timeout_ms, uint8_t max_retry)
{
    if (!cmd) {
        ESP_LOGE(LORA_TAG, "AT cmd is NULL");
        return false;
    }

    for (uint8_t retry = 0; retry < max_retry; retry++)
    {
        // Clear the receive buffer
        while (LoraSerial.available()) LoraSerial.read();

        LoraSerial.println(cmd);

        // Waiting for response
        uint32_t t_start = millis();
        String response = "";
        while (millis() - t_start < (uint32_t)timeout_ms)
        {
            if (LoraSerial.available())
            {
                char c = LoraSerial.read();
                response += c;
                if (c == '\n')
                {
                    response.trim();
                    if (response == "OK") {
                        ESP_LOGI(LORA_TAG, "[OK] %s", cmd);
                        return true;
                    }
                    if (response == "ERROR") {
                        ESP_LOGE(LORA_TAG, "[ERROR] %s", cmd);
                        break;      // Jump out of the inner while and enter retry
                    }
                    response = "";  // Clear, continue reading the next line
                }
            }
        }
        ESP_LOGE(LORA_TAG, "[Timeout/Retry %d] %s", retry + 1, cmd);
    }
    return false;
}

// ======================== First configuration ========================

bool LoraManager::configure()
{
    if (isConfigured()) {
        ESP_LOGI(LORA_TAG, "Lora already configured, skip");
        return true;
    }

    ESP_LOGI(LORA_TAG, "First time config...");

    // Pull CE low to enter configuration mode
    digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);

    for (int i = 0; i < LORA_INIT_CMD_COUNT; i++)
    {
        if (!sendAT(LORA_INIT_CMDS[i])) {
            ESP_LOGE(LORA_TAG, "Config failed at: %s", LORA_INIT_CMDS[i]);
            digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
            return false;
        }
    }

    // Pull CE high to return to normal working mode
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
    // Record configured
    setConfigured(true);
    ESP_LOGI(LORA_TAG, "Config done");
    return true;
}

// ======================== Combination interface ========================

void LoraManager::setup()
{
    init();
    configure();
}

// ======================== Send wake-up frame ========================

void LoraManager::sendWakeFrame()
{
    protocol_frame_t frame;
    frame_build(&frame, MASTER_FRAME_HEAD, FRAME_WAKE);

    // Problem: Switching the level here will cause the first transmission to fail and cause the check digit data to be incorrect.
    // digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);
    // vTaskDelay(pdMS_TO_TICKS(100)); // TODO: Make sure to enter configuration mode, remember it is 2ms, add 10ms here just in case?
    LoraSerial.write((uint8_t*)&frame, sizeof(frame));
    // digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
}

// ======================== Close ========================

void LoraManager::shutdown()
{
    digitalWrite(LORA_POWER_PIN, LORA_POWER_OFF);
    LoraSerial.end();
}

// Lora enables the ce pin, CE is low level, and enters the sending data and configuration mode.
void LoraManager::enable_ce()
{
    digitalWrite(LORA_CE_PIN, LORA_CE_INACTIVE);
}

// Lora disables the ce pin, CE is high, and enters the receiving data mode.
void LoraManager::disable_ce()
{
    digitalWrite(LORA_CE_PIN, LORA_CE_ACTIVE);
}