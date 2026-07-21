#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <Arduino.h>

// ======================== Frame header (1 byte can distinguish master from slave) ========================
#define MASTER_FRAME_HEAD       0xA5
#define SLAVE_FRAME_HEAD        0x5A

// ======================== Frame type ========================
enum frame_type_t : uint8_t {
    FRAME_MASTER_MATCH  = 0x01,     // Host pairing request
    FRAME_SLAVE_MATCH   = 0x02,     // Slave pairing response
    FRAME_WAKE          = 0x03,     // Host wakes up wirelessly
    FRAME_WAKE_ACK      = 0x04,     // Slave wake-up response
    FRAME_RADAR_DATA    = 0x05,     // Radar data frame
    FRAME_END           = 0x06,     // end frame
};

// ======================== Protocol frame (8 bytes, 4 bytes aligned) ========================
//  [head 1B][type 1B][dist 2B][angle 2B][reserve 1B][checksum 1B] = 8B
typedef struct __attribute__((packed)) {
    uint8_t      head;          // Frame header 0xA5 / 0x5A
    frame_type_t type;          // Frame type
    uint16_t     dist;          // Distance (cm)
    int16_t      angle;         // Angle (×0.01°)
    uint8_t      reserve;       // Reserved
    uint8_t      checksum;      // Checksum = sum of first 7 bytes & 0xFF
} protocol_frame_t;

static_assert(sizeof(protocol_frame_t) == 8, "protocol_frame_t must be 8 bytes");

// ======================== Utility functions ========================
//
// Usage example:
//
//   // Build frame
//   protocol_frame_t frame;
//   frame_build(&frame, MASTER_FRAME_HEAD, FRAME_WAKE);
//   frame_build(&frame, SLAVE_FRAME_HEAD, FRAME_RADAR_DATA, dist_mm, angle);
//
//   // Check frame
//   if (frame_validate(data, len, SLAVE_FRAME_HEAD, FRAME_WAKE_ACK)) {
//       // The frame is legal
//   }
//

// Calculate frame check value
uint8_t frame_calc_checksum(const protocol_frame_t* frame);

// Build frame (automatically fill reserve + checksum)
void frame_build(protocol_frame_t* frame, uint8_t head, frame_type_t type,
                 uint16_t dist = 0, int16_t angle = 0);

// Verify frame validity
bool frame_validate(const uint8_t* data, int len,
                    uint8_t expect_head, frame_type_t expect_type);

#endif
