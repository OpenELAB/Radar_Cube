// 所有的gpio引脚和应用相关的配置参数，可以通过init函数输入每个模块，也可以模块内部直接读取宏定义
// 核心思路是模块化代码，这样保持主程序里面逻辑代码简洁干净，便于代码维护，以及后续代码复用
// 可以用c++的类，也可以用c风格进行模块化，核心思路是一样的，习惯哪种就用哪种
// 每个模块我设计了基本的api函数，可以根据实际需求自行修改和增删。但是注意提供给外部使用的函数和给模块内部使用的函数的区分。
// 还有就是这些模块是可能需要复用给车内车外两个不同设备使用的，不同的gpio，主控等，实现的时候注意兼容性
// 这里面列举的模块的函数api的参数列表，返回值都按需修改
// 新的模块按需添加，比如一些工具模块，或者数据处理模块等

class BeeperController {
    // 控制蜂鸣器，通过外部给定的模式进行对应的鸣叫
    void init();
    void beep(enum BEEP);
    // ... 可能还有其他函数或者鸣叫模式需要补充，按需添加
};

class RadarController {
    // 这个是雷达模块特有的模块
    // 这个模块负责和传感器模块的交互，读取数据，并且进行相应的滤波处理，
public: 
    void init();
    void read_distance(); // 纯距离
    void readData(); // 距离和角度
private:
    void filter_data();
    void get_raw_data();
};

class NVSManager {
    // 统一进行nvs数据管理，读和写
    // 可以使用arduino平台的preferences库，也可以使用espidf原生nvs库
    // 可以内部定义有哪些需要读写的nvs数据，外部调用时传入数据/变量名和数据值，nvsmanager负责管理nvs对应的字段然后读写数据
    void write_data(); 
    void read_data(); 
    void clear_data();
};

class LoraModule {
    // 负责lora模块的管理，初始化，设置不同模式。以及收发信息
    void init();
    void set_wake_up_mode();
    void send_data();
    void receive_data();
    // ... 可能还有其他函数需要补充，按需添加
};

class LEDControler {
    // 所有对led的管理进行封装，初始化gpio，提供闪烁和呼吸灯两种可以设置频率的显示效果
    // 如果需要后台保持对灯的控制，需要创建rtos任务
    void init();
    void blink(int period);
    void breath(int speed);
    // ... 可能还有其他函数需要补充，按需添加
};

class PowerManager {
    // 把睡眠相关的代码都放到这个模块进行统一操作，保持住文件里代码逻辑简洁
    // 负责对唤醒条件进行设置,进入睡眠，以及提供唤醒理由（如果需要）
    void set_wakeup();
    void sleep();
    void get_wakeup_reason();
    // ... 可能还有其他函数需要补充，按需添加
};

class ComManager {
    // 使用底层ESPNow进行通信，管理通信
    // 1. peer/mac/device信息管理, 配对信息写入NVSManager
    // 2. 主程序外部构造好基本数据包payload，commanager负责构造数据包并且发送出去
    // 3. 负责通信协议管理，外部调用者不参与这些细节处理
    // 这部分比较复杂，可以先实现最核心功能，之后慢慢补充
    void init();
    // 这两个函数分别提供给车内车外模块使用，车外模块作为master主动发出配对请求，雷达模块等待pair请求
    void request_pair();
    void wait_pair_request();
    void is_paired();
    // ... 可能还有其他函数需要补充，按需添加
};

class ESPNow {
    // 封装esp now通信底层接口，只负责收发信息，不负责内容处理.
    // 使用callback函数对收发的数据进行处理，避免阻塞，通过freertos的queue队列存储收到的数据包
    // 提供api给commanager使用
    // 参考 https://github.com/xiaochutan123l/DetectiveBadge/blob/main/src/esp_now_network.c, 或者找官方espnow例程来封装
    void init();
    void send_packet();
    void recv_packet();
    void del_peer(const uint8_t *peer_addr);
    // ... 可能还有其他函数需要补充，按需添加
};


// 还有一个重要部分是通信部分的协议设计，lora和espnow通信格式都应该遵循统一协议规则. 这个等后续强化通信功能的时候再改
struct RC_Packet {
    // Radar_Cube Packet structure
    uint8_t magic_num;
    uint8_t pkt_type;
    uint8_t pkt_len;
    uint8_t seq_num;
    uint32_t data;
}

// 另一个是按钮管理部分的，这部分暂时先直接写主程序里面，之后再封装