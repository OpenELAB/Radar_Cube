#include <Arduino.h>
#include "config.h"
#include "radar.h"

#ifdef OUTSIDE

RadarModule* RadarModule::_instance = nullptr;

// ======================== 初始化 / 关闭 ========================

void RadarModule::init()
{
    _instance = this;

    RadarSerial.begin(115200, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
    RadarSerial.onReceive(onSerialRxStatic);

    pinMode(RADAR_POWER_PIN, OUTPUT);
    digitalWrite(RADAR_POWER_PIN, RADAR_POWER_ON);
}

void RadarModule::shutdown()
{
    digitalWrite(RADAR_POWER_PIN, RADAR_POWER_OFF);
    while (RadarSerial.available()) RadarSerial.read();
    RadarSerial.end();
}

// ======================== FIFO ========================

bool RadarModule::fifoPush(uint8_t b)
{
    uint16_t next = (_fifo_head + 1) % FIFO_SIZE;
    if (next == _fifo_tail) return false;   // 满了
    _fifo[_fifo_head] = b;
    _fifo_head = next;
    return true;
}

bool RadarModule::fifoPop(uint8_t* b)
{
    if (_fifo_head == _fifo_tail) return false;   // 空的
    *b = _fifo[_fifo_tail];
    _fifo_tail = (_fifo_tail + 1) % FIFO_SIZE;
    return true;
}

// ======================== 串口回调 ========================

void RadarModule::onSerialRxStatic()
{
    if (_instance) _instance->onSerialRx();
}

void RadarModule::onSerialRx()
{
    while (RadarSerial.available()) {
        fifoPush(RadarSerial.read());
    }
}

// ======================== 校验 & 解析 ========================

// 校验：前12字节之和的低8位 == 第12字节（下标12）
bool RadarModule::verifyChecksum(const uint8_t* raw)
{
    uint8_t sum = 0;
    for (int i = 0; i < 12; i++) {
        sum += raw[i];
    }
    // checksum 在帧的最后2字节（下标12-13），取低字节比较
    return (sum == raw[12]);
}

bool RadarModule::parseFrame(const uint8_t* raw, RadarFrame_t* out)
{
    // 帧头检查
    if (raw[0] != 0xAA || raw[1] != 0x55) {
        ESP_LOGW(RADAR_TAG, "header error");
        return false;
    }

    memcpy(out, raw, sizeof(RadarFrame_t));

    // 数据长度检查
    if (out->len != RADAR_DATA_LEN) {
        ESP_LOGW(RADAR_TAG, "len error");
        return false;
    }

    // 校验和检查
    if (!verifyChecksum(raw)) {
        ESP_LOGW(RADAR_TAG, "checksum error");
        return false;
    }

    return true;
}

// ======================== 主循环解析 ========================

void RadarModule::loop()
{
    uint8_t b;
    while (fifoPop(&b))
    {
        switch (_state)
        {
        case WAIT_AA:
            if (b == 0xAA) {
                _rxCnt = 0;
                _rxBuf[_rxCnt++] = b;
                _state = WAIT_55;
            }
            break;

        case WAIT_55:
            _rxBuf[_rxCnt++] = b;
            _state = (b == 0x55) ? RECV_PAYLOAD : WAIT_AA;
            break;

        case RECV_PAYLOAD:
            _rxBuf[_rxCnt++] = b;
            if (_rxCnt >= RADAR_FRAME_LEN)
            {
                RadarFrame_t fr;
                if (parseFrame(_rxBuf, &fr)) {
                    _latest.dist_mm   = fr.dist;
                    _latest.angle_deg = fr.angle * 0.01f;
                    _data_ready = true;
                    ESP_LOGI(RADAR_TAG, "dist: %d mm, angle: %.2f deg",
                             _latest.dist_mm, _latest.angle_deg);
                }
                _state = WAIT_AA;
            }
            break;
        }
    }
}

bool RadarModule::getData(RadarData* out)
{
    if (!_data_ready || !out) return false;
    *out = _latest;
    _data_ready = false;
    return true;
}

#endif

