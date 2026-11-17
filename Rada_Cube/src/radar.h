#ifndef __RADAR_H__
#define __RADAR_H__

#ifdef OUTSIDE

#include <Arduino.h>
#include "pins.h"

// ======================== Radar data frame ========================
#define RADAR_FRAME_LEN     14      // Total frame length (bytes)
#define RADAR_DATA_LEN      6       // Data segment length

typedef struct __attribute__((packed)) {
    uint16_t head;          // Frame header 0xAA55
    uint16_t cmd;           // Command
    uint16_t len;           // Data length
    uint16_t dist;          // Distance (mm)
    int16_t  angle;         // Angle (x0.01°)
    uint16_t reserve;       // Reserved
    uint16_t checksum;      // Verification
} RadarFrame_t;

// Analyzed radar data (convenient for upper layer use)
struct RadarData {
    uint16_t dist_mm;       // Distance mm
    float    angle_deg;     // angle degree
};

// ======================== Radar module class ========================
/**
 * Radar sensor module (only used for exterior modules)
 *
 * Usage example:
 *
 *   RadarModule Radar;
 *
 *   Radar.init();           // Initialize serial port + turn on power
 *   while (running) {
 *       Radar.loop();       // Get data from FIFO and parse it (non-blocking)
 *       RadarData rd;
 *       if (Radar.getData(&rd)) {
 *           // rd.dist_mm, rd.angle_deg is the latest data
 *       }
 *   }
 *   Radar.shutdown();       // Turn off radar
 */
class RadarModule
{
public:
    // Initialize serial port + power supply
    void init();
    // Turn off radar
    void shutdown();
    // Called in the main loop, fetch data from FIFO and parse it
    void loop();
    // Get the latest frame analysis result, return true to indicate there is new data (the flag is automatically cleared after reading)
    bool getData(RadarData* out);

private:
    // ---- Latest analysis results ----
    RadarData _latest{};
    bool      _data_ready = false;
    // ---- FIFO ring buffer ----
    static constexpr size_t FIFO_SIZE = 512;
    uint8_t  _fifo[FIFO_SIZE];
    volatile uint16_t _fifo_head = 0;   // Note: uint16_t is required because FIFO_SIZE=512 > 255
    volatile uint16_t _fifo_tail = 0;

    bool fifoPush(uint8_t b);
    bool fifoPop(uint8_t* b);

    // ---- Frame parsing state machine ----
    enum State : uint8_t { WAIT_AA, WAIT_55, RECV_PAYLOAD };
    State   _state = WAIT_AA;
    uint8_t _rxBuf[RADAR_FRAME_LEN];
    uint8_t _rxCnt = 0;

    // Check: sum of first 12 bytes == 13th byte (lower 8 bits)
    bool verifyChecksum(const uint8_t* raw);
    // Parse the complete frame
    bool parseFrame(const uint8_t* raw, RadarFrame_t* out);

    // ---- Serial port callback ----
    static RadarModule* _instance;
    static void onSerialRxStatic();
    void onSerialRx();
};

#endif // OUTSIDE
#endif // __RADAR_H__
