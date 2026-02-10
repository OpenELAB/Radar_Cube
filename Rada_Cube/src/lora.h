#ifndef __LORA_H__
#define __LORA_H__

#include <Preferences.h>


class LoraManager
{
public:
    void flag_ifconfig();
    void lora_init();
    bool lora_config();
    bool get_lora_flag();
    void write_lora_flag(bool flag);
    bool clear_lora_key();
    bool at_send_wait_reponse(const char* cmd, int timeout, uint8_t maxretry = 3);
    void lora_end();
    
#ifdef INSIDE
    bool lora_wakeup();
    bool lora_sleep_mode();
#endif

#ifdef OUTSIDE

#endif

private:
    Preferences lora_prefe;
};



#endif
