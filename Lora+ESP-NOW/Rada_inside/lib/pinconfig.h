#ifndef __PINCONFIG_H__
#define __PINCONFIG_H__

#include <Arduino.h>
#include <string>

#define LED_PIN              3
#define WAKE_PIN             5
#define BUZZER_PIN           4
#define BUZZER_FREQ          2700
#define BUZZER_RESOLUTION    8
#define LORA_RX_PIN          6 
#define LORA_TX_PIN          7
#define LORA_CE_PIN          10

inline constexpr uint8_t MATCH_SUCCESS[] = "MATCH_SUCCESS_ACK";
inline constexpr size_t  MATCH_SUCCESS_SIZE = sizeof(MATCH_SUCCESS) - 1;

enum SYSTEM_MODE
{
    NOMAL_MODE = 0,
    TEST_MODE,
    WORK_MODE,
    RECONFIG_MODE
};


#endif
