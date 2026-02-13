#ifndef __RADAR_H__
#define __RADAR_H__

#ifdef OUTSIDE

#include <Arduino.h>
#include "pins.h"

// ======================== 雷达数据帧 ========================
#define RADAR_FRAME_LEN     14      // 帧总长度（字节）
#define RADAR_DATA_LEN      6       // 数据段长度

typedef struct __attribute__((packed)) {
    uint16_t head;          // 帧头 0xAA55
    uint16_t cmd;           // 命令
    uint16_t len;           // 数据长度
    uint16_t dist;          // 距离 (mm)
    int16_t  angle;         // 角度 (x0.01°)
    uint16_t reserve;       // 预留
    uint16_t checksum;      // 校验
} RadarFrame_t;

// 解析后的雷达数据（方便上层使用）
struct RadarData {
    uint16_t dist_mm;       // 距离 mm
    float    angle_deg;     // 角度 度
};

// ======================== 雷达模块类 ========================
/**
 * 雷达传感器模块（仅车外模块使用）
 *
 * 使用示例：
 *
 *   RadarModule Radar;
 *
 *   Radar.init();           // 初始化串口 + 开电源
 *   while (running) {
 *       Radar.loop();       // 从 FIFO 取数据并解析（非阻塞）
 *       RadarData rd;
 *       if (Radar.getData(&rd)) {
 *           // rd.dist_mm, rd.angle_deg 即为最新数据
 *       }
 *   }
 *   Radar.shutdown();       // 关闭雷达
 */
class RadarModule
{
public:
    // 初始化串口 + 电源
    void init();
    // 关闭雷达
    void shutdown();
    // 主循环中调用，从 FIFO 取数据并解析
    void loop();
    // 获取最新一帧解析结果，返回 true 表示有新数据（读后自动清除标志）
    bool getData(RadarData* out);

private:
    // ---- 最新解析结果 ----
    RadarData _latest{};
    bool      _data_ready = false;
    // ---- FIFO 环形缓冲区 ----
    static constexpr size_t FIFO_SIZE = 512;
    uint8_t  _fifo[FIFO_SIZE];
    volatile uint16_t _fifo_head = 0;   // 注意：必须 uint16_t，因为 FIFO_SIZE=512 > 255
    volatile uint16_t _fifo_tail = 0;

    bool fifoPush(uint8_t b);
    bool fifoPop(uint8_t* b);

    // ---- 帧解析状态机 ----
    enum State : uint8_t { WAIT_AA, WAIT_55, RECV_PAYLOAD };
    State   _state = WAIT_AA;
    uint8_t _rxBuf[RADAR_FRAME_LEN];
    uint8_t _rxCnt = 0;

    // 校验：前12字节之和 == 第13字节（低8位）
    bool verifyChecksum(const uint8_t* raw);
    // 解析完整帧
    bool parseFrame(const uint8_t* raw, RadarFrame_t* out);

    // ---- 串口回调 ----
    static RadarModule* _instance;
    static void onSerialRxStatic();
    void onSerialRx();
};

#endif // OUTSIDE
#endif // __RADAR_H__
