#ifndef __LORA_H__
#define __LORA_H__

#include <Preferences.h>


class LoraManager
{
public:
    // NVS 管理
    // 判断NVS里关于Lora标志位的KEY
    void flag_ifconfig();
    // 获取Lora配置的标志位
    bool get_lora_flag();
    // 写入Lora配置的标志位
    void write_lora_flag(bool flag);
    // 清除Lora标志位的KEY
    bool clear_lora_key();

    // 初始化Lora模块，串口及打开Lora模块的功率开关
    void lora_init();
    // Lora模块的配置逻辑
    bool lora_setting();
    // AT指令发送及等待返回值
    bool at_send_wait_reponse(const char* cmd, int timeout, uint8_t maxretry = 3);

    // 唤醒从机
    void wireless_wake_up();

    // ================================ ESP睡眠后变为高阻态，功率开关有下拉电阻，理论上硬件就可以控制，软件上是否需要在加上？ ==============================================
    // 关闭串口
    void lora_end();

    // Lora配置的总逻辑
    void lora_config();


#ifdef OUTSIDE

#endif

private:
    Preferences lora_prefe;

#endif

private:
    Preferences prefe;
};



#endif
