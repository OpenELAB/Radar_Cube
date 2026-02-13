#ifndef __RADAR_H__
#define __RADAR_H__


#ifdef OUTSIDE

// 雷达数据帧的长度
#define RADAR_FRAME_LEN     14
// 雷达数据的长度
#define RADAR_DATA_LEN      6

// 雷达数据帧结构，取消字节对齐
#pragma pack(1)
typedef struct 
{
    uint16_t head;      //  数据帧头 
    uint16_t cmd;       //  命令 
    uint16_t len;       //  数据长度
    uint16_t dist;      //  距离
    int16_t angle;     //  角度
    uint16_t reserve;   //  预留字节
    uint16_t crc;       //  CRC校验码
}RadarFrame_t;
#pragma pack()

struct RadarData
{
    uint16_t dist;
    float angle_deg;
};

class RadarModule
{
public:
    static constexpr size_t FIFO_SIZE = 512;

    // 状态机
    enum State : uint8_t {HUNT_AA, HUNT_55, PAYLOAD};
    // 入队函数
    bool fifoPut(uint8_t b);
    //  出队函数
    bool fifoGet(uint8_t* b);
    //  校验和验证函数
    bool verifyChecksum(const uint8_t* raw, size_t len = 12);
    //  数据帧解析函数
    bool parseRadarFrame(const uint8_t* raw, RadarFrame_t* f);

    // 串口对应的函数
    void Radar_init();
    void Radar_end();
    
    //  Radar串口接收回调函数
    static void Radar_onReceiveStatic();
    void Radar_onReveive();

    void radar_loop();
    
    
private:
    // 状态机状态和接收数组
    State state = HUNT_AA;
    uint8_t rxBuf[14];
    uint8_t rxCnt = 0;

    // 队列环形缓冲区
    uint8_t fifoBuf[FIFO_SIZE];
    volatile uint8_t fifo_head =0;
    volatile uint8_t fifo_tail = 0;

    // 单例指针
    static RadarModule* instance;

};

#endif




#endif
