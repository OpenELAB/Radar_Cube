#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "radar.h"

#ifdef OUTSIDE

RadarModule* RadarModule::instance = nullptr;

void RadarModule::Radar_onReceiveStatic()
{
    if(instance != nullptr)
    {
        instance->Radar_onReveive();
    }
}

void RadarModule::Radar_init()
{
    instance = this;

    RadarSerial.begin(115200, SERIAL_8N1, RADAR_RX_PIN,RADAR_TX_PIN);
    RadarSerial.onReceive(Radar_onReceiveStatic);
    // 打开雷达传感器的功率开关
    pinMode(RADAR_POWER_PIN, OUTPUT);
    digitalWrite(RADAR_POWER_PIN, RADAR_POWER_ON);
}

void RadarModule::Radar_end()
{
    // ================================ 接了下拉电阻应该可以省略 ================================
    // 关闭雷达传感器的功率开关
    digitalWrite(RADAR_POWER_PIN, LOW);
    
    // 清空串口缓冲区
    while(RadarSerial.available()) RadarSerial.read();
    RadarSerial.end();
}

// 入队
bool RadarModule::fifoPut(uint8_t b)
{
    size_t next = (fifo_head + 1) % FIFO_SIZE;
    if(next == fifo_tail)
    {
        // 队列已经满了
        return false;
    }
    fifoBuf[fifo_head] = b;
    fifo_head = next;
    return true;
}

// 出队
bool RadarModule::fifoGet(uint8_t* b)
{
    if(fifo_head == fifo_tail)
    {
        // 对列是空的
        return false;
    }
    *b = fifoBuf[fifo_tail];
    fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
    return true;
}

// CRC校验
bool RadarModule::verifyChecksum(const uint8_t* raw, size_t len)
{
    uint8_t checksum = 0;
    for(size_t i = 0; i < len -1; i++)
    {
        checksum += raw[i];
    }
    return checksum == raw[len];
}

// 数据帧解析函数，
bool RadarModule::parseRadarFrame(const uint8_t* raw, RadarFrame_t* f)
{
    // 检验帧头
    if(raw[0] != 0xAA || raw[1] != 0x55)
    {
        ESP_LOGI(RADAR_TAG, "header error");
        return false;
    }
    memcpy(f, raw, sizeof(RadarFrame_t));

    // 检验数据长度
    if(f->len != RADAR_DATA_LEN)
    {
        ESP_LOGI(RADAR_TAG, "len error");
        return false;
    }

    // 检验校验和
    if(!verifyChecksum(raw))
    {
        ESP_LOGI(RADAR_TAG, "checksum error");
        return false;
    }

    return true;
}

// 串口接收回调函数
 void RadarModule::Radar_onReveive()
{
    while(RadarSerial.available())
    {
        uint8_t c = RadarSerial.read();
        fifoPut(c);
    }
}



void RadarModule::radar_loop()
{
    uint8_t b;
    while(fifoGet(&b))
    {
        switch(state)
        {
            case HUNT_AA:
            {

                if(b == 0xAA)
                {
                    state = HUNT_55;
                    rxCnt = 0;
                    rxBuf[rxCnt++] = b;
                }
                break;
            }
            case HUNT_55:
            {
                rxBuf[rxCnt++] = b;
                if(b == 0x55)
                {
                    state = PAYLOAD;
                }
                else
                {
                    state = HUNT_AA;
                }
                break;
            }
            case PAYLOAD:
            {
                rxBuf[rxCnt++] = b;
                if(rxCnt == RADAR_FRAME_LEN)
                {
                    RadarFrame_t fr;
                    if(parseRadarFrame(rxBuf, &fr))
                    {
                        uint16_t dist = fr.dist;
                        int16_t angle = fr.angle;
                        float angle_deg = angle * 0.01f;
                        ESP_LOGI(RADAR_TAG, "dist: %d, angle_deg: %f", dist,  angle_deg);
                    }
                    state = HUNT_AA;
                }
                break;
            }
        }
    }
}






















#endif








