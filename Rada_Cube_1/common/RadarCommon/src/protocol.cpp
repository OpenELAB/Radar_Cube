#include "protocol.h"
#include <cstring>

// 计算校验值：前 7 字节之和 & 0xFF
uint8_t frame_calc_checksum(const protocol_frame_t* frame)
{
    const uint8_t* p = (const uint8_t*)frame;
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) {
        sum += p[i];
    }
    return sum;
}

uint8_t lora_wake_frame_calc_checksum(const lora_wake_frame_t* frame)
{
    const uint8_t* p = (const uint8_t*)frame;
    uint8_t sum = 0;
    for (int i = 0; i < (int)sizeof(lora_wake_frame_t) - 1; i++) {
        sum += p[i];
    }
    return sum;
}

// 构建帧
void frame_build(protocol_frame_t* frame, uint8_t head, frame_type_t type,
                 uint16_t dist, int16_t angle)
{
    frame->head     = head;
    frame->type     = type;
    frame->dist     = dist;
    frame->angle    = angle;
    frame->reserve  = 0;
    frame->checksum = frame_calc_checksum(frame);
}

void lora_wake_frame_build(lora_wake_frame_t* frame, const uint8_t master_mac[6])
{
    frame->head = MASTER_FRAME_HEAD;
    frame->type = FRAME_WAKE;
    memcpy(frame->master_mac, master_mac, 6);
    frame->checksum = lora_wake_frame_calc_checksum(frame);
}

// 校验帧合法性
bool frame_validate(const uint8_t* data, int len,
                    uint8_t expect_head, frame_type_t expect_type)
{
    if (len != (int)sizeof(protocol_frame_t)) return false;

    const protocol_frame_t* f = (const protocol_frame_t*)data;
    if (f->head != expect_head) return false;
    if (f->type != expect_type) return false;
    if (f->checksum != frame_calc_checksum(f)) return false;

    return true;
}

bool lora_wake_frame_validate(const uint8_t* data, int len)
{
    if (len != (int)sizeof(lora_wake_frame_t)) return false;

    const lora_wake_frame_t* f = (const lora_wake_frame_t*)data;
    if (f->head != MASTER_FRAME_HEAD) return false;
    if (f->type != FRAME_WAKE) return false;
    if (f->checksum != lora_wake_frame_calc_checksum(f)) return false;

    return true;
}
