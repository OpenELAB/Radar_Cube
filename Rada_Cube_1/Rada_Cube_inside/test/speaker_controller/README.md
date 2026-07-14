# SpeakerController 独立实机测试

本测试直接编译和调用生产 `src/speaker_controller.h/.cpp`，不复制驱动源码。软件状态由串口自动断言；音频是否完整、循环、恢复、静音以及是否存在爆音必须结合实物或视频确认。

## 音频来源

资源定义以仓库根目录下列内容为准：

- `tts_to_wav/data`
- `tts_to_wav/音频与音效清单.md`

测试使用：

- `/boot.wav`：0.589 秒，16 kHz、单声道、16-bit PCM WAV。
- `/beep_slow.wav`：3.201 秒，16 kHz、单声道、16-bit PCM WAV。
- `/tone_success_up.wav`：0.328 秒，16 kHz、单声道、16-bit PCM WAV。

烧录 LittleFS 前只临时复制 `tone_success_up.wav`，测试后删除。

## 编译与烧录

在 `Rada_Cube_inside` 目录执行：

1. 将 `tts_to_wav/data/tone_success_up.wav` 临时复制到 `Rada_Cube_inside/data`。
2. 执行 `pio run -t uploadfs --upload-port COM6`。
3. 删除临时 `Rada_Cube_inside/data/tone_success_up.wav`。
4. 临时将 `src/main.ino` 改名为 `src/main.ino.old`。
5. 将 `test/speaker_controller/speakercontroller_entry.ino` 复制为 `src/speakercontroller.ino`。
6. 执行 `pio run` 和 `pio run -t upload --upload-port COM6`。
7. 执行 `pio device monitor -p COM6 -b 115200`，输入 `a` 运行完整测试。
8. 测试结束输入 `x`，删除运行时损坏 WAV 并关闭 Controller。
9. 删除临时 `src/speakercontroller.ino`，将 `src/main.ino.old` 恢复为 `src/main.ino`。

禁止提交步骤 1、3、4、5、9 产生的临时改动。

如果 esptool 提示 `No serial data received`，先按住板上 BOOT，点按 RESET，
再松开 BOOT，使 ESP32-C6 进入下载模式后重新执行上传命令。

## 串口菜单

- `a`：完整测试。
- `1`：once；`2`：loop；`3`：once 打断 loop 并恢复。
- `4`：连续 once；`5`：stop；`6`：异常路径。
- `7`：音量；`8`：快速覆盖；`9`：生命周期及 10 轮压力测试。
- `p`：状态；`s`：停止；`x`：清理测试文件并结束；`h`：帮助。

## 自动检查

- [x] `begin()/end()` 幂等，10 轮压力测试通过。
- [x] 播放中 end 后 task、I2S 输出和功放正确退出。
- [x] once 接受后立即 busy，完成后回到 Silent。
- [x] loop 不计 busy，持续循环且路径保持正确。
- [x] once 打断 loop 后恢复相同 loop。
- [x] 新 once 覆盖旧 once，只有最新 once 正常完成。
- [x] stop 后不恢复旧 loop。
- [x] nullptr 和空路径拒绝且不改变播放状态。
- [x] 不存在文件和损坏 WAV 设置失败状态、进入 Silent 且不重试。
- [x] stop 不清除失败状态，新播放命令清除失败状态。
- [x] 音量 255 钳制到 100，播放中音量更新不覆盖 loop。
- [x] 600 次快速 once/loop/stop 后无死锁、崩溃、异常复位或 busy 残留。

## 听感检查

输入 `a` 后，预期按以下顺序听到音频：

0. 生命周期测试：`beep_slow` 开始播放约 0.5 秒后被 `end()` 停止，随后无残留声音。
1. once 测试：`boot` 从头到尾完整播放一次，时长约 0.589 秒，结束后静音。
2. loop 测试：`beep_slow` 连续循环约 7 秒，可听到两个以上完整周期，随后停止。
3. loop 恢复测试：先播放 `beep_slow`，约 0.9 秒后切换到 0.328 秒的 `tone_success_up`；提示音结束后，`beep_slow` 自动恢复并继续循环，随后停止。
4. once 覆盖测试：`boot` 开始约 0.1 秒后被 `tone_success_up` 打断；只允许新的 `tone_success_up` 完整结束，旧 `boot` 不得恢复。
5. stop 测试：先播放 `beep_slow`，再切换到 `boot`；`boot` 开始约 0.1 秒后执行 `stop()`，之后至少 4.2 秒保持静音，旧 `beep_slow` 不得恢复。
6. 异常恢复测试：不存在文件和损坏 WAV 均不应产生有效声音；异常状态清除后，`tone_success_up` 应能再次完整播放一次。
7. 音量测试：依次以 0%、50%、100% 各播放一次 `tone_success_up`，预期分别为静音、适中、明显更响；随后 `beep_slow` 在不中断循环的情况下按 50%（约 1 秒）→0%（约 1.1 秒）→50%（约 1.1 秒）变化，最后停止。
8. 快速覆盖测试：600 次 once/loop/stop 快速覆盖期间可能只听到短促片段；最终必须保持静音，不得持续播放、卡死或出现异常残留声音。

- [x] boot 完整播放一次，无明显爆音。
- [x] beep_slow 连续播放超过两个文件周期。
- [x] success tone 插入 beep loop 后，相同 beep loop 自动恢复。
- [x] 连续 once 时 boot 被打断，只有 success tone 完成。
- [x] stop 后等待超过一个 loop 周期仍保持静音。
- [x] 0% 静音、50% 适中、100% 更响且无明显失真。
- [x] loop 播放中 50%→0%→50% 能静音并恢复，不中断 loop。
- [x] 播放中 end 后无残留声音。

## 当前结果

- 生产环境编译：通过（2026-07-14，ESP32-C6 / Arduino 3.3.9，全量干净编译）。
- LittleFS 上传：通过（2026-07-14，COM6，写入后哈希校验通过）。
- 固件烧录：通过（2026-07-14，COM6，写入后哈希校验通过）。
- 自动串口检查：通过，`100/100 checks passed`。
- 10 轮生命周期压力测试：通过，10 轮均为 PASS。
- 测试清理：通过，运行时损坏 WAV 已删除，Controller 已结束，生产 main 和 data 已恢复。
- 实物听感：通过，完整播放、循环、打断恢复、stop 静音、音量变化及无明显爆音或残留声音均已确认正常。
- 视频证据：测试通过后附到 PR 或团队沟通渠道，不提交到仓库。
