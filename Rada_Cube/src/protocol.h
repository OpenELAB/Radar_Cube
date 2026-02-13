#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <Arduino.h>

//通信协议帧
// 帧头
#define MASTER_FRAME_HEAD              0xA5A5
#define SLAVE_FRAME_HEAD               0x5A5A


// 帧类型
typedef enum
{
    MASTER_MATCH_FRAME          = 0x01,
    SLAVE_MATCH_FRAME           = 0x02,
    MASTER_WIRELESS_WAKE_FRAME  = 0x03,
    SLAVE_WAKE_ACK_FRAME        = 0x04,
    RADAR_DATA_FRAME            = 0x05,
    END_FRAME                   = 0x06
}frame_type_t;

// 数据长度 6
#define FRAME_DATA_LEN                  6

// mac匹配时帧的数据
#define MAC_MATCH_DATA                  0
#define WIRELESS_WAKE_DATA              0
// 预留值也是0
#define RESERVE_DATA                    0

// CRC校验的长度，为前12字节之和的低八位
#define CRC_CALC_LEN                    12


// 协议帧结构体
typedef struct __attribute__((packed))
{
    uint16_t head;      // 帧头
    uint16_t type;  // 帧类型
    uint16_t len;       // 数据长度
    uint16_t dist;      // 距离
    int16_t angle;      // 角度
    uint16_t reserve;   // 预留值
    uint16_t crc;       // CRC校验
}protocol_frame_t;



// CRC校验函数
uint16_t crc_set(protocol_frame_t* frame);





#endif
