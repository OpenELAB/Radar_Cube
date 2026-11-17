// All gpio pins and application-related configuration parameters can be entered into each module through the init function, or macro definitions can be read directly inside the module. The key point is that two device firmwares reuse the same module to implement as lib. The two device firmwares only need different macro definitions and logic in main.
// The core idea is to modularize the code, so as to keep the logic code in the main program simple and clean, which facilitates code maintenance and subsequent code reuse.
// You can use C++ classes, or you can use C style for modularization. The core idea is the same. Use whichever one you are used to.
// I designed basic API functions for each module, which can be modified, added or deleted according to actual needs. But pay attention to the distinction between functions provided for external use and functions used internally in the module.
// In addition, these modules may need to be reused for two different devices inside and outside the car, with different gpio, main control, etc. Pay attention to compatibility when implementing them.
// The parameter list of the function api of the module listed here, and the return value is modified as needed.
// New modules are added as needed, such as some tool modules or data processing modules, etc.


// You can choose whether the enum should be placed outside the class or inside the class, and the naming can also be modified appropriately.
enum BEEPMODE {
    NONE,
    SLOW_BEEP,
    NORMAL_BEEP,
    FAST_BEEP,
    RISE_TONE_CONT, // continous
    DOWN_TONE_CONT,
    RISE_TONE_DIST, // discret
    DOWN_TONE_DIST
};

class BeeperController {
    // Control the buzzer and make corresponding beeps through the externally given mode.
    void init();
    void beep(enum BEEP);
    // ... There may be other functions or tweet modes that need to be added, add them as needed
};

class RadarController {
    // This is a module unique to the radar module
    // This module is responsible for interacting with the sensor module, reading data, and performing corresponding filtering.
public: 
    void init();
    void read_distance(); // Pure distance
    void readData(); // Distance and angle
private:
    void filter_data();
    void get_raw_data();
};

class NVSManager {
    // Unified nvs data management, reading and writing
    // You can use the preferences library of the arduino platform or the espidf native nvs library.
    // You can internally define what nvs data needs to be read and written. When calling externally, pass in the data/variable name and data value. nvsmanager is responsible for managing the fields corresponding to nvs and then reading and writing the data.
    void write_data(); 
    void read_data(); 
    void clear_data();
};

class LoraModule {
    // Responsible for the management, initialization and setting of different modes of the lora module. and send and receive messages
    void init();
    void set_wake_up_mode();
    void send_data();
    void receive_data();
    // ... There may be other functions that need to be added, add them as needed
};

class LEDControler {
    // Encapsulate all LED management, initialize gpio, and provide two display effects of flashing and breathing lights that can set the frequency.
    // If you need to maintain control of the lights in the background, you need to create an rtos task
    void init();
    void blink(int period);
    void breath(int speed);
    // ... There may be other functions that need to be added, add them as needed
};

enum BATTERY_STAT {
    NORMAL,
    LOW_ENGERGY,
    NO_ENGERGY
}

class PowerManager {
    // Put all sleep-related codes into this module for unified operation, and keep the code logic in the file simple.
    // Responsible for setting wake-up conditions, entering sleep, and providing reasons for wake-up (if necessary)
    // Responsible for battery power management
    void set_wakeup();
    void sleep();
    void get_wakeup_reason();
    // ... There may be other functions that need to be added, add them as needed
    void get_battery_value();
};

class ComManager {
    // Use the underlying ESPNow to communicate and manage communication
    // 1. peer/mac/device information management, pairing information is written to NVSManager
    // 2. The basic data packet payload is constructed outside the main program, and the commanager is responsible for constructing the data packet and sending it out.
    // 3. Responsible for communication protocol management, external callers do not participate in these details.
    // This part is relatively complicated. You can implement the core functions first and then add them slowly.
    void init();
    // These two functions are provided to the in-vehicle and out-of-vehicle modules respectively. As the master, the out-of-vehicle module actively sends a pairing request, and the radar module waits for the pair request.
    void request_pair();
    void wait_pair_request();
    void is_paired();
    // ... There may be other functions that need to be added, add them as needed
};

class ESPNow {
    // Encapsulates the underlying interface of esp now communication, which is only responsible for sending and receiving information, and is not responsible for content processing.
    // Use the callback function to process the sent and received data to avoid blocking, and store the received data packets through the freertos queue.
    // Provide API for use by commanager
    // Refer to https://github.com/xiaochutan123l/DetectiveBadge/blob/main/src/esp_now_network.c, or find the official espnow routine to encapsulate
    void init();
    void send_packet();
    void recv_packet();
    void del_peer(const uint8_t *peer_addr);
    // ... There may be other functions that need to be added, add them as needed
};

enum SYSTEM_MODE {
    NORMAL_MODE,
    UNPAIR_MODE,
    PAIR_MODE,
    TEST_MODE,
    CONFIG_MODE,
    UNKNOW
};

class ModeManager {
    // Responsible for determining what mode is currently entered based on buttons, reasons for sleep and wake-up, and pairing status
    void init(); // Initialize gpio
    void check_mode(); 
    void set_isr(); // Set userbutton interrupt
    void stop_by_user(); // Check whether the user presses the button to interrupt the reversing process
}

// Another important part is the protocol design of the communication part. Both lora and espnow communication formats should follow unified protocol rules. This will be changed later when the communication function is strengthened.
struct RC_Packet {
    // Radar_Cube Packet structure
    uint8_t magic_num;
    uint8_t pkt_type;
    uint8_t pkt_len;
    uint8_t seq_num;
    uint32_t data;    // ACK data should contain radar module power supply data
}

// The other one is the button management part. This part is written directly into the main program for the time being, and then encapsulated later.