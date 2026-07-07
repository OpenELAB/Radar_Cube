问题：音效声效的决策在哪里进行？main还是驱动层？这部分没有定义明确 -》feedbackcontroller

集中讨论视听效果种类和语义，以及事件类型

格式：事件类型，灯效，音效，阻塞（冲突隐患）
1. 切歌/顺序播放，one-shot/wait
## 场景 - 事件Event
### UNPAIR
上电，未配对
Event：未配对 
    led：熄灭左右两个灯
    speaker：
        playonce:mode_unpaired.wav
        wait等待播放完成
        打断

### PAIRED_MODE
Event：进入配对模式
    led：
        上下led blink green
        2Hz
    speaker：
        playonce：mode_pairing.wav
        wait等待播放完成
        打断

Event: 左侧配对成功
    led：
        左灯常亮green + 上下green灯闪烁
        2Hz
    speaker：
        playonce:pair_ok_left.wav
        wait

Event: 右侧配对成功
    led：
        右灯常亮green + 左灯常亮green + 上下 green blink
        2Hz
    speaker：
        playonce:pair_ok_right.wav
        wait
# TODO
更优雅的灯效设计

Event：双侧配对成功
    led: 4灯常亮
    speaker: 
        playonce: pair_ok_both.wav  (+ready for xxx)
        wait

Event: 配对结果:左失败
    led：leftBlinkRed + right常亮+ 上下green blink
        2Hz
    speaker: 
        playonce:pair_fail_left.wav
        wait

Event: 配对结果:right失败
    led：rightBlinkRed  + left常亮 + 上下green blink
        2Hz
    speaker: 
        playonce:pair_fail_right.wav
        wait

Event: 配对结果:both失败
    led：4 led blinkRed
    speaker: 
        playonce:pair_fail_both.wav
        wait

Event: 请重新配对
    led：x
    speaker: 
        playonce:请重新配对.wav（TODO）
        wait
等待用户重新进入配对模式

### WORK_MODE
Event: 开始唤醒
    led: 上下两个灯常亮green
    speaker: 
        playonce: wake_start.wav
        wait

Event：左侧唤醒成功
    led: 上下两个灯常亮green + left green 常亮

Event：右侧唤醒成功
    led: 上下两个灯常亮green + right green 常亮

Event：唤醒成功（双侧）
    led: 4led常亮green
    speaker: 
        playonce：wake_ok.wav
        wait

### TEST_MODE
### CONFIG_MODE
### FACTORY_RESET_MODE

### 通用
Event: boot
    led: 跑马灯转一圈
        wait
    speaker：
        playonce：sys_boot.wav
        wait

Event：shutdown
    led：跑马灯逆时针一圈
        wait
    speaker：
        playonce：sys_shutdown.wav
        wait


## FeedbackController API

- onXXEvent() {
    led.blink(2hz)
    speaker.palyonce(WAKE_OK)
}
- onXXEventWait()

## Driver API

### RGB
- blink
- breath
- fade
- leftsolid(coler=green) / leftsolidgreen
- leftBlinkRed
- ..


### Speaker
- speaker driver
    - palyonce(WAKE_OK) {
        play(wake_ok_path)
    }
- audio management
    - audio.h (enum WAKE_OK)
    - audio_macro <-> audio_path

### 设计原则
main.ino
负责业务流程：配对、唤醒、工作、睡眠

FeedbackController
负责用户反馈策略：什么事件该亮什么灯、播什么声音、谁打断谁

RgbLedController
负责 RGB 显示执行：设置颜色、闪烁、状态显示

SpeakerController
负责声音播放执行：播放 WAV、循环、停止、音量、I2S

### TODO：
0. 明确feedbackcontroller所有行为种类，以及明确rgb和speaker行为边界
1. 设计feedbackcontroller，rgbled和speaker controller骨架结构（函数api）
2. main标记责任错位代码
2.1 rebled根据设计实现
2.2 speaker根据设计实现
2.3 feedbackcontroller根据设计实现
3. 清理main错位代码
4. main调用feedbackcontroller实现逻辑
