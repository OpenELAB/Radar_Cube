#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <Arduino.h>

// ======================== 帧头（1 字节即可区分主从） ========================
#define MASTER_FRAME_HEAD       0xA5
#define SLAVE_FRAME_HEAD        0x5A

// ======================== 帧类型 ========================
enum frame_type_t : uint8_t {
    FRAME_MASTER_MATCH  = 0x01,     // 主机配对请求
    FRAME_SLAVE_MATCH   = 0x02,     // 从机配对应答
    FRAME_WAKE          = 0x03,     // 主机无线唤醒
    FRAME_WAKE_ACK      = 0x04,     // 从机唤醒应答
    FRAME_RADAR_DATA    = 0x05,     // 雷达数据帧
    FRAME_END           = 0x06,     // 结束帧
    FRAME_STANDBY       = 0x07,     // Return outside unit to BLE standby
    FRAME_STANDBY_ACK   = 0x08,     // Standby command acknowledged
    FRAME_WAKE_CONFIRM  = 0x09,     // Master confirms matching wake session
};

// ======================== 协议帧（8 字节，4 字节对齐）========================
//  [head 1B][type 1B][dist 2B][angle 2B][reserve 1B][checksum 1B] = 8B
typedef struct __attribute__((packed)) {
    uint8_t      head;          // 帧头 0xA5 / 0x5A
    frame_type_t type;          // 帧类型
    uint16_t     dist;          // 距离 (cm)
    int16_t      angle;         // 角度 (×0.01°)
    uint8_t      reserve;       // 预留
    uint8_t      checksum;      // 校验 = 前 7 字节之和 & 0xFF
} protocol_frame_t;

static_assert(sizeof(protocol_frame_t) == 8, "protocol_frame_t must be 8 bytes");

// ======================== 工具函数 ========================
//
// 使用示例：
//
//   // 构建帧
//   protocol_frame_t frame;
//   frame_build(&frame, MASTER_FRAME_HEAD, FRAME_WAKE);
//   frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA, dist_cm, angle);
//
//   // 校验帧
//   if (frame_validate(data, len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
//       // 帧合法
//   }
//

// 计算帧校验值
uint8_t frame_calc_checksum(const protocol_frame_t* frame);

// 构建帧（自动填充 reserve + checksum）
void frame_build(protocol_frame_t* frame, uint8_t head, frame_type_t type,
                 uint16_t dist = 0, int16_t angle = 0);

// Control frames reuse the 32-bit dist/angle payload for the BLE wake session.
void frame_build_session(protocol_frame_t* frame, uint8_t head,
                         frame_type_t type, uint32_t session_id);
uint32_t frame_get_session(const protocol_frame_t* frame);

// 校验帧合法性
bool frame_validate(const uint8_t* data, int len,
                    uint8_t expect_head, frame_type_t expect_type);

#endif
