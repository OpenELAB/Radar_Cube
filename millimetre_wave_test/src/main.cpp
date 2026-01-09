// #include <Arduino.h>
// #include <espnow.h>
// #include <sr04.h>

// ESPNOW_Rada rada_broad;
// SR04 rada_sensor;

// void setup()
// {
//     Serial.begin(115200);
//     rada_broad.Rada_rece_init();
//     rada_broad.unicast_mode();
//     rada_sensor.sr04_init();


// }

// void loop()
// {
//     uint16_t d = rada_sensor.get_distance();
//     if(d> 20 && d < 600) 
//     {
//         Serial.println(d);
//         rada_broad.send_rada_data((uint8_t*)&d, sizeof(d));
//     }

//     delay(20);

// }


// #include <Arduino.h>

// #define TRIG   4
// #define ECHO   5


// #define ALPHA 0.2           // 低通滤波系数，0.1~0.3 建议
// #define PULSE_TIMEOUT 40000 // 40ms 超时（JSN-SR04T 推荐）

// float filteredDistance = -1; // 初始状态

// void setup() {
//   Serial.begin(115200);
//   pinMode(TRIG, OUTPUT);
//   pinMode(ECHO, INPUT); // 不用 INPUT_PULLUP
//   digitalWrite(TRIG, LOW);
//   delay(50);
// }

// int readDistanceOnce() {
//   // 触发 10us
//   digitalWrite(TRIG, LOW);
//   delayMicroseconds(2);

//   digitalWrite(TRIG, HIGH);
//   delayMicroseconds(10);
//   digitalWrite(TRIG, LOW);

//   unsigned long duration = pulseIn(ECHO, HIGH, PULSE_TIMEOUT);
//   if (duration == 0) return 0; // 无回波
//   int cm = duration / 58;
//   if (cm < 20) cm = 20;        // JSN-SR04T 的盲区
//   if (cm > 600) cm = 600;      // 最大范围
//   return cm;
// }

// void loop() {
//   int d = readDistanceOnce(); 
//   if (d == 0) return;  // 无回波就跳过

//   // --- 一阶低通滤波（关键）---
//   if (filteredDistance < 0) {
//     filteredDistance = d;  // 初始化
//   } else {
//     filteredDistance = filteredDistance + ALPHA * (d - filteredDistance);
//   }

//   Serial.print("Distance-cm: ");
//   Serial.println(filteredDistance);

//   delay(60);  // JSN-SR04T 推荐 ≥ 50ms
// }




// #include <Arduino.h>

// #define TRIG   4
// #define ECHO   5





// void setup()
// {
//     Serial.begin(115200);
//     Serial.println("Rada Sensor Test Start");
//     pinMode(TRIG, OUTPUT);
//     pinMode(ECHO, INPUT);
// }
// void loop()
// {
//     digitalWrite(TRIG, LOW);
//     delayMicroseconds(2);
//     digitalWrite(TRIG, HIGH);
//     delayMicroseconds(20);
//     digitalWrite(TRIG, LOW);
                                                 
//     unsigned long duration = pulseIn(ECHO, HIGH, 38000);
//     int cm = duration / 58;
//     Serial.print("Distance-cm: ");
//     Serial.println(cm);


//     delay(100);
// }




// // ——————————  UART 自动串口模式  ——————————
// #include <Arduino.h>
// #include <HardwareSerial.h>

// #define TRIG   4
// #define ECHO   5

// #define ALPHA 0.2

// float filteredDistance = -1; // 初始状态


// HardwareSerial Rada_Serial(1);
// void onRadaReceive()
// {
//     while (Rada_Serial.available() >= 4)
//     {
//         uint8_t buffer[4];
//         Rada_Serial.readBytes(buffer, 4);
//         if(buffer[0] == 0xFF)
//         {
//             uint16_t distance = (buffer[1] << 8) | buffer[2];
//             uint8_t checksum = (buffer[0] +buffer[1] + buffer[2]) & 0xFF;
//             if (checksum == buffer[3])
//             {
//                 float distamce_cm = distance / 10.0;

//                 // ------ 一阶低通滤波器 ------
//                 if(filteredDistance < 0)
//                 {
//                     filteredDistance = distamce_cm;
//                 }
//                 else
//                 {
//                     filteredDistance = filteredDistance + ALPHA * (distamce_cm - filteredDistance);
//                 }

//                 Serial.print("Distance-cm: ");
//                 Serial.println(filteredDistance);
//             }
//             else
//             {
//                 Serial.println("校验失败");
//             }
//         }
//         else
//         {
//             Rada_Serial.read();
//         }
//     }
// }
// void setup()
// {
//     Serial.begin(115200);
//     Rada_Serial.begin(9600, SERIAL_8N1, ECHO, TRIG);
//     Serial.println("Rada Auto Serial Mode Test Start!");
//     Rada_Serial.onReceive(onRadaReceive);
// }

// void loop()
// {
// }





// ——————————  普通触发测距模式  ——————————
// #define ALPHA 0.2           // 低通滤波系数，0.1~0.3 建议
// #define PULSE_TIMEOUT 40000 // 40ms 超时（JSN-SR04T 推荐）

// float filteredDistance = -1; // 初始状态

// void setup() {
//   Serial.begin(115200);
//   pinMode(TRIG, OUTPUT);
//   pinMode(ECHO, INPUT); // 不用 INPUT_PULLUP
//   digitalWrite(TRIG, LOW);
//   delay(50);
// }

// int readDistanceOnce() {
//   // 触发 10us
//   digitalWrite(TRIG, LOW);
//   delayMicroseconds(2);

//   digitalWrite(TRIG, HIGH);
//   delayMicroseconds(10);
//   digitalWrite(TRIG, LOW);

//   unsigned long duration = pulseIn(ECHO, HIGH, PULSE_TIMEOUT);
//   if (duration == 0) return 0; // 无回波
//   int cm = duration / 58;
//   if (cm < 20) cm = 20;        // JSN-SR04T 的盲区
//   if (cm > 600) cm = 600;      // 最大范围
//   return cm;
// }

// void loop() {
//   int d = readDistanceOnce(); 
//   if (d == 0) return;  // 无回波就跳过

//   // --- 一阶低通滤波（关键）---
//   if (filteredDistance < 0) {
//     filteredDistance = d;  // 初始化
//   } else {
//     filteredDistance = filteredDistance + ALPHA * (d - filteredDistance);
//   }

//   Serial.print("Distance-cm: ");
//   Serial.println(filteredDistance);

//   delay(60);  // JSN-SR04T 推荐 ≥ 50ms
// }



#include <Arduino.h>
#include <HardwareSerial.h> 

#define TRIG   5
#define ECHO   4
#define ALPHA 0.3

float filteredDistance = -1;
float filteredAngle = -1;

HardwareSerial Rada_Serial(1);

// 环形缓冲区
constexpr size_t Rada_FIFO_SIZE = 512;
uint8_t fifo[Rada_FIFO_SIZE];
volatile size_t fifo_head = 0;
volatile size_t fifo_tail = 0;

// 入队
static inline bool fifoPut(uint8_t b)
{
    size_t next = (fifo_head + 1) % Rada_FIFO_SIZE;
    if(next == fifo_tail) return false; // 队列满了
    fifo[fifo_head] = b;
    fifo_head = next;
    return true;
}

// 出队
static inline bool fifoGet(uint8_t *b)
{
    if(fifo_head == fifo_tail) return false; // 队列空的
    *b = fifo[fifo_tail];
    fifo_tail = (fifo_tail + 1) % Rada_FIFO_SIZE;
    return true;
}

// 校验和
static bool verifychecksum(const uint8_t* raw, size_t len = 12)
{
    uint8_t sum = 0;
    for(size_t i = 0; i < len -1; i++)
    {
        sum += raw[i];
    }
    return sum == raw[len];
}

// 雷达帧结构
#pragma pack(1)
typedef struct
{
    uint16_t head; // 帧头
    uint16_t cmd;  // 命令
    uint16_t len;  // 数据长度
    uint16_t dist; // 距离
    uint16_t angle; // 角度
    uint16_t reserve; // 预留
    uint16_t crc; // CRC校验
}RadarFrame_t;
#pragma pack()


// 数据帧解析函数
bool parseRadarFrame(const uint8_t* raw, RadarFrame_t* f)
{
    if(raw[0] != 0xAA || raw[1] != 0x55) return false;
    memcpy(f, raw, sizeof(RadarFrame_t));
    if(f->len != 6) return false;
    if(!verifychecksum(raw)) return false;
    return true;
}

// 接收回调函数
void Rada_onReceive()
{
    while(Rada_Serial.available() > 0)
    {
        uint8_t b = Rada_Serial.read();
        fifoPut(b);
    }
}

//  状态机
enum : uint8_t{HUNT_AA, HUNT_55, PAYLOAD};
uint8_t state = HUNT_AA;
uint8_t rxBuf[14];
uint8_t rxCnt = 0;

void setup()
{
    Serial.begin(115200);
    Rada_Serial.begin(115200, SERIAL_8N1, ECHO, TRIG);
    Serial.println("Rada Sensor Test Start !");
    Rada_Serial.onReceive(Rada_onReceive);

}

void loop()
{
    uint8_t b;
    while (fifoGet(&b))
    {
        switch(state)
        {
            case HUNT_AA:
                if(b == 0xAA)
                {
                    state = HUNT_55;
                    rxCnt = 0;
                    rxBuf[rxCnt++] = b;
                }
                break;
            case HUNT_55:
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
            case PAYLOAD:
                rxBuf[rxCnt++] = b;
                if(rxCnt == 14)
                {
                    RadarFrame_t fr;
                    if(parseRadarFrame(rxBuf, &fr))
                    {
                        uint16_t dist = fr.dist;
                        int16_t angle = (int16_t)fr.angle;
                        float angle_deg = angle * 0.01f;


                        // if(filteredDistance < 0)
                        // {
                        //     filteredDistance = dist;
                        // }
                        // else
                        // {
                        //     filteredDistance = filteredDistance + ALPHA * (dist - filteredDistance);
                        // }
                        Serial.print("Distance:");
                        Serial.println(dist);

                        // for(int i = 0; i < 14; i++)
                        // {
                        //     Serial.printf("%02X ", rxBuf[i]);
                        // }
                        // Serial.println();
                        // float angle_rad = angle_deg * PI / 180.0f;
                        // float x_dist = dist * cos(angle_rad);
                        // Serial.printf("Distance: %d cm, Angle: %.2f deg\r\n", dist, angle_deg);
                        // Serial.printf("distant: %.2f cm\n", x_dist);
                        // Serial.print("distant: ");
                        // Serial.println(x_dist);

                        // // 测试，直接对距离数据进行滤波
                        // if(dist < 20.0f)
                        // {
                        //     dist = 20.0f; // 最小距离
                        // }
                        // else if(dist > 250.0f)
                        // {
                        //     dist = 250.0f; // 最大距离
                        // }
                        // if(filteredDistance < 0)
                        // {
                        //     filteredDistance = dist;
                        // }
                        // else
                        // {
                        //     filteredDistance  = filteredDistance + ALPHA * (dist - filteredDistance);
                        // }
                        // Serial.print("distant: ");
                        // Serial.println(filteredDistance);

                        //先算出距离，然后进行滤波
                        // // 异常数据处理
                        // if(x_dist < 20.0f)
                        // {
                        //     x_dist = 20.0f;
                        // }
                        // else if(x_dist > 250.0f)
                        // {
                        //     x_dist = 250.0f;
                        // }
                        // // 一阶低通滤波
                        // if(filteredDistance < 0)
                        // {
                        //     filteredDistance = x_dist;
                        // }
                        // else
                        // {
                        //     filteredDistance  = filteredDistance + ALPHA * (x_dist - filteredDistance);
                        // }
                        // Serial.print("distant: ");
                        // Serial.println(filteredDistance);

                        // // 先对距离和角度分别滤波，在计算水平坐标
                        // if(angle_deg > 60.0f || angle_deg < -60.0f)
                        // {
                        //     // 超出有效角度范围
                        //     state = HUNT_AA;
                        //     break;
                        // }
                        // if(dist < 20.0f)
                        // {
                        //     dist = 20.0f;
                        // }
                        // else if(dist > 250.0f)
                        // {
                        //     dist = 250.0f;
                        // }
                        // // 距离滤波
                        // if(filteredDistance < 0)
                        // {
                        //     filteredDistance = dist;
                        // }
                        // else
                        // {
                        //     filteredDistance  = filteredDistance + ALPHA * (dist - filteredDistance);
                        // }
                        // // 角度滤波
                        // if(filteredAngle < 0)
                        // {
                        //     filteredAngle = angle_deg;
                        // }
                        // else
                        // {
                        //     filteredAngle  = filteredAngle + ALPHA * (angle_deg - filteredAngle);
                        // }
                        // // 计算水平坐标
                        // float angle_rad = filteredAngle * PI / 180.0f;
                        // float x_dist = filteredDistance * cos(angle_rad);
                        // Serial.print("distant: ");
                        // Serial.println(x_dist);
                    }
                    state = HUNT_AA;
                }
                break;
            
        }
    }
    
}



