#include "protocol.h"



// 计算CRC校验值
uint16_t crc_set(protocol_frame_t* frame)
{
    uint8_t* data = (uint8_t*)frame;
    uint16_t sum = 0;
    for(uint8_t i = 0; i < CRC_CALC_LEN; i++)
    {
        sum += data[i];
    }
    return(sum & 0x00FF);
}