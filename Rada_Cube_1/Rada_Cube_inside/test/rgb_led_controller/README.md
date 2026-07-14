# RgbLedController 独立实机测试

本测试直接编译和调用生产 `src/rgb_led_controller.h/.cpp`，不复制驱动源码。串口断言只验证可由软件观察的 API 与状态；灯区、动画方向和灯效节奏必须结合实物确认。

## 编译与烧录

在 `Rada_Cube_inside` 目录执行以下步骤：

1. 临时将 `src/main.ino` 改名为 `src/main.ino.old`。
2. 将 `test/rgb_led_controller/rgbcontroller_entry.ino` 复制为 `src/rgbcontroller.ino`。
3. 执行 `pio run` 和 `pio run -t upload`。
4. 以 115200 波特率打开串口，输入 `a` 运行完整测试。
5. 测试结束后删除临时 `src/rgbcontroller.ino`，并将 `src/main.ino.old` 恢复为 `src/main.ino`。

不要提交第 1、2、5 步产生的临时 `src` 目录改动。

## 完整测试与生命周期压力测试

输入 `a` 会运行一次完整测试，包括：

- 一轮 `begin()/end()` 生命周期检查。
- API 参数、非法枚举与亮度钳制检查。
- 全部灯区及 `off()` 检查。
- blink/breathe 三档速度检查。
- 四种有限动画、`isBusy()` 和动画抢占检查。
- 600 次快速命令覆盖检查。

完整测试结束时，串口摘要应显示 `54/54 checks passed`。其中 `[PASS]` 是软件自动检查结果，所有 `[OBSERVE]` 项仍需通过实物或视频确认。

输入一次 `l` 只运行一轮生命周期测试：调用两次 `end()`，检查 begun、task、RGB 供电和命令拒绝状态；随后调用两次 `begin()`，检查 begun、task 与 RGB 供电恢复。它不验证灯区、频率或动画。

连续输入十个 `l`：

```text
llllllllll
```

即可执行连续 10 轮生命周期压力测试，主要验证 task、NeoPixel 和 RMT 资源能否稳定重复创建和释放。每轮应输出 8 项 `[PASS]`，十轮共 80 项，且不得出现 RMT 错误、内存不足、异常复位或卡死。

在 PlatformIO 中可按以下方式手动执行：

```powershell
pio device monitor -p COM6 -b 115200
```

进入串口后直接输入 `llllllllll`。如果使用的串口终端不会立即发送字符，则输入后按回车；测试程序会忽略换行。测试结束后输入 `p`，预期状态为：

```text
[STATE] begun=1 task_running=1 busy=0 brightness=64 power_pin=1
```

生命周期测试结束时会重新调用 `begin()`，因此最终 task 和 RGB 供电保持开启。

## 自动检查

- [x] `begin()/end()` 重复调用保持幂等。
- [x] `end()` 后 task 停止、RGB 供电关闭，且拒绝新灯效命令。
- [x] `begin()` 后 task 运行、RGB 供电开启。
- [x] 亮度输入钳制到 `RGB_LED_MAX_BRIGHTNESS`。
- [x] 等待执行的灯效命令与后续亮度命令正确合并。
- [x] 非法 zone、speed、animation 返回 `false`。
- [x] 四种有限动画运行时 `isBusy()` 为 `true`，完成后恢复 `false`。
- [x] 持续灯效抢占动画后及时清除 busy。
- [x] 600 次快速覆盖命令全部被接受，task 保持运行且最终状态正确。

串口摘要必须显示所有 `[AUTOMATED]` 检查通过；出现任一 `[FAIL]` 都不能判定测试通过。

## 实物检查

- [x] `LED_ZONE_ALL`：四灯亮。
- [x] `LED_ZONE_ONLY_LEFT`：仅左灯亮。
- [x] `LED_ZONE_ONLY_RIGHT`：仅右灯亮。
- [x] `LED_ZONE_ONLY_SIDES`：左右两灯亮，上下灯不亮。
- [x] `LED_ZONE_LEFT`：左灯、上灯、下灯亮。
- [x] `LED_ZONE_RIGHT`：右灯、上灯、下灯亮。
- [x] `LED_ZONE_NONE` 与 `off()`：四灯熄灭。
- [x] blink 慢、中、快三档约为 1000/500/200 ms 周期。
- [x] breathe 慢、中、快三档约为 2400/1600/900 ms 周期。
- [x] 顺时针跑马灯：`BOTTOM -> LEFT -> TOP -> RIGHT`，绿色，约 1 秒。
- [x] 逆时针跑马灯：`BOTTOM -> RIGHT -> TOP -> LEFT`，绿色，约 1 秒。
- [x] `FLASH`：四灯绿色快速闪烁 3 次。
- [x] `FADE_OUT`：柔和琥珀色按 `RIGHT -> TOP -> LEFT -> BOTTOM` 渐暗。
- [x] 动画被常亮抢占后立即显示四灯蓝色，不恢复旧动画。
- [x] 快速覆盖结束后显示四灯绿色，无卡死、异常复位或闪烁残留。
- [x] `end()` 后四灯熄灭且 RGB 供电关闭。

## 当前结果

- 生产环境编译：通过（2026-07-14，ESP32-C6 / Arduino 3.3.9）。
- 固件烧录：通过（2026-07-14，ESP32-C6，COM6）。
- 自动串口检查：通过，54/54 checks passed；额外连续 10 轮 `end()/begin()` 压力测试通过。
- 实物观察：通过，全部灯区、频率、动画及抢占效果已确认正确。

首次实测发现重复 `end()/begin()` 后 GPIO4 的 RMT 通道未重新附着。生产 Controller 已改为每个生命周期独立创建和销毁 NeoPixel 实例，修复后重复生命周期及完整自动测试均未再出现 RMT 错误。
