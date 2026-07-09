# FeedbackController 视听反馈设计

## 核心原则

1. `main` 只负责业务流程和事件判断，不直接控制 RGB 或 Speaker。
2. `FeedbackController` 负责视听策略：灯效、音频、打断、恢复和冲突仲裁。
3. `RgbLedController` 和 `SpeakerController` 只负责执行底层硬件动作。
4. 所有 `onXxxEvent()` 都立即返回，不在函数内部等待声音或动画结束。
5. `main` 决定业务节奏：要保留前一个反馈就先等 `isBusy()` 或超时；要抢占就直接调用下一个事件。
6. 除倒车距离显示外，新事件被调用就可以替换或打断前一个视听反馈。
7. 倒车距离显示期间不插入长语音，只允许短音效短暂打断距离蜂鸣。
8. `playOnce()` 打断 `playLoop()` 后自动恢复 loop；这个通用播放语义属于 SpeakerController。
9. FeedbackController 第一版不需要 `update()`，不维护提示队列；需要顺序播放时由 main 控制调用时机。
10. 音频 ID 和路径放在 AudioCatalog；SpeakerController 只接收音频路径，不认识 AudioId。

## 灯环模型

四颗 RGB 灯采用菱形排列，并通过柔光板形成一个灯环：

| 物理灯位 | 语义 |
| --- | --- |
| 左灯 | 左侧车外模块 |
| 右灯 | 右侧车外模块 |
| 上灯、下灯 | 公共灯环区域，用于距离、模式和整体状态 |

`SensorSet` 表达当前可用的外部模块：

| SensorSet | 含义 |
| --- | --- |
| `None` | 没有可用外部设备 |
| `Left` | 只有左侧设备可用 |
| `Right` | 只有右侧设备可用 |
| `Both` | 左右设备均可用 |

`LedZone` 只表达灯效作用到哪个固定灯区，不包含业务含义。
实现上直接传 enum，具体控制哪几颗物理灯由 RgbLedController 内部判断：

| 场景 | SensorSet | LedZone |
| --- | --- | --- |
| 配对/唤醒单侧成功 | `Left` | `LED_ZONE_ONLY_LEFT` |
| 配对/唤醒单侧成功 | `Right` | `LED_ZONE_ONLY_RIGHT` |
| 左右设备灯同时提示 | `Both` | `LED_ZONE_ONLY_SIDES` |
| 工作距离反馈 | `Left` | `LED_ZONE_LEFT` |
| 工作距离反馈 | `Right` | `LED_ZONE_RIGHT` |
| 工作距离反馈 | `Both` | `LED_ZONE_ALL` |
| 全灯动画 | 任意 | `playAnimation(...)` |

RGB 驱动只需要一个 task。每一帧根据当前效果统一渲染，不为左右灯分别创建独立动态效果。
例如 `blink()` 只维护一个闪烁节拍；每次节拍翻转时，根据 `LedZone` 决定实际写四颗灯、三颗灯还是单颗灯。

## 灯语约定

颜色语义保持稳定：

| 颜色 | 语义 |
| --- | --- |
| 蓝色 | 配对、配置、等待 |
| 绿色 | 正常、成功、可用 |
| 黄色/琥珀色 | 注意、降级、失联、低电 |
| 红色 | 危险、失败、系统不可用 |

动态语义保持稳定：

| 动态 | 语义 |
| --- | --- |
| 顺时针跑马灯 | 启动、进入 |
| 逆时针渐暗 | 退出、关机、休眠 |
| 呼吸 | 等待连接或等待用户 |
| 单次脉冲 | 全局完成动画或明确失败提示 |
| 闪烁 | 距离变化、警告、失败 |
| 常亮 | 稳定状态 |

同一时刻只运行一种普通动态效果，不叠加左右两套不同的闪烁或呼吸。
RGB 驱动只提供物理动画名，例如 `RGB_ANIMATION_CHASE_CLOCKWISE`；
启动、关机、成功、失败等业务语义只存在于 FeedbackController 的事件映射中。

## 音频约定

音频分三类：

| 类型 | 用途 |
| --- | --- |
| 距离蜂鸣 | 持续表达当前最小距离 |
| 短提示音 | 成功、恢复、掉线等状态变化 |
| 最终语音 | 配对结果、关机、严重异常或用户行动指导 |

除倒车距离显示外，`main` 调用新的 `onXxxEvent()` 就表示新事件优先，FeedbackController 可以替换或打断前一个反馈。

倒车距离显示是唯一例外：

```text
SpeakerController 正在 playLoop(distance_beep)
FeedbackController 调用 playOnce(short_tone)
SpeakerController 播放 short_tone
SpeakerController 自动恢复最新 playLoop(distance_beep)
```

倒车距离显示期间不得插入长语音。掉线、恢复、低电等提示需要防抖和冷却，短时间内左右同时发生同类异常时应合并成一次双侧提示。

AudioCatalog 只负责资源映射，例如 `AUDIO_ID_WAKE_OK -> "/wake_ok.wav"`。
FeedbackController 根据事件和当前模式选择 `AudioId`，再通过 `audio_path_from_id()` 把路径传给 SpeakerController。
SpeakerController 不判断这个路径是唤醒提示、配对结果还是距离蜂鸣。

倒车距离显示状态由 FeedbackController 维护。在这个状态下：

- 距离蜂鸣使用 `speaker.playLoop(audio_path_from_id(AUDIO_ID_BEEP_xxx))`。
- 只有短提示音可以用 `speaker.playOnce(audio_path_from_id(AUDIO_ID_TONE_xxx))` 临时插入。
- 长语音提示不得插入，必须等待退出距离显示后再播放，或直接丢弃/合并。
- 播放前可用 `audio_is_blocked_during_parking(audio_id)` 判断是否属于倒车期间黑名单。
- playOnce 播完后是否恢复 loop，由 SpeakerController 根据“期间是否有新命令”判断。

## 等待规则

`wait` 不是音频类型，只是 main 的业务选择。

- 如果 main 希望当前反馈完整播放，就先等待 `FeedbackController` 空闲或超时。
- 如果 main 直接调用下一个 `onXxxEvent()`，就允许 FeedbackController 替换或打断前一个反馈。
- 等待期间不能停止无线接收、RGB task、Speaker task 或系统维护任务。

适合 main 等待的场景：

- 配对最终结果。
- 关机或进入深度睡眠。

不适合 main 等待的场景：

- 工作模式距离更新。
- 单侧掉线或恢复。
- 单侧配对或唤醒成功。
- 双侧唤醒完成反馈；第一版由 main 控制是否等待上升音和 `wake_ok.wav`。

## 模块职责

### main

- 创建和初始化 RGB、Speaker、FeedbackController。
- 判断业务事件并调用 `feedback.onXxxEvent()`。
- 需要顺序播放时，只通过 `feedback.isBusy()` 控制下一次事件调用时机。
- 不直接调用 RGB/Speaker 的业务灯效或音频 API。

### FeedbackController

- 维护当前 `SensorSet` 和必要的反馈状态。
- 根据事件选择灯效、音频和作用区域。
- 使用 AudioCatalog 中的 `AudioId` 查路径，再发出 playOnce/playLoop/stop 指令。
- 维护是否处于倒车距离显示状态，并阻止长语音插入距离蜂鸣。
- 不维护提示队列。

### RgbLedController

- 驱动四颗 RGB 灯。
- 执行颜色、动态效果和 `LedZone`。
- 只提供物理灯效 API，不出现启动、关机、成功、失败等业务命名。
- 不理解配对、距离、失联或系统模式。

### SpeakerController

- 执行一次播放、循环播放、停止和音量控制。
- 如果 `playOnce()` 打断当前 `playLoop()`，一次播放结束后自动恢复最新 loop。
- 如果 playOnce 期间收到新的播放命令，则不恢复旧 loop。
- 管理 WAV 读取、I2S、功放和播放状态。
- 不决定业务优先级或用户反馈策略，也不认识 AudioId。
