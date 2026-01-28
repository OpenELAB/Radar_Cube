#ifndef __LORA_H__
#define __LORA_H__

#include <Preferences.h>


class LoraManager
{
public:
    void init();
    bool lora_config();
    bool at_send_wait_reponse(const char* cmd, int timeout, uint8_t maxretry = 3);
#ifdef INSIDE
    bool lora_wakeup();
    bool lora_sleep_mode();
#endif

private:
    Preferences prefe;

};




#endif
