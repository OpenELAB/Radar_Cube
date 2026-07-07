#include "protocol.h"

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
