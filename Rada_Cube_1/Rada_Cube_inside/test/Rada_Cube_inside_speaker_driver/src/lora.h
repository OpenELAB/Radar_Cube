#ifndef __LORA_H__
#define __LORA_H__

#include <Preferences.h>
#include "pins.h"
#include "config.h"

/**
 * Lora wireless module management
 *
 * Usage example:
 *
 *   LoraManager Lora;
 *
 *   // Initialization + AT configuration (automatically skipped if already configured)
 *   Lora.setup();
 *
 *   // Send wake-up frame
 *   Lora.sendWakeFrame();
 *
 *   // Close the module
 *   Lora.shutdown();
 */
class LoraManager
{
public:
    // Initialize serial port + CE pin + power supply
    void init();

    // Configure AT parameters for the first time (skip if already configured), return whether successful
    bool configure();

    // Combination of init() + configure(), called to main
    void setup();

    // Send AT command and wait for OK / ERROR, support retry
    bool sendAT(const char* cmd, int timeout_ms = LORA_AT_TIMEOUT, uint8_t max_retry = LORA_AT_RETRY);

    // Send wireless wake-up frame through Lora
    void sendWakeFrame();

    // Turn off the Lora module (pull down the power + turn off the serial port)
    void shutdown();

    // Clear configured flags in NVS (for debugging/resetting)
    void clearConfigFlag();

    // CE control pin, enable the CE pin for sending, disable it for receiving
    void enable_ce();
    void disable_ce();

private:
    Preferences _prefs;

    // NVS related
    bool isConfigured();
    void setConfigured(bool flag);
};

#endif
