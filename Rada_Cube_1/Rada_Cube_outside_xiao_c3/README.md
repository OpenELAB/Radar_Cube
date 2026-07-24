# XIAO ESP32C3 车外低功耗验证固件

这是第一阶段的独立车外测试工程，不修改现有车内工程和原车外工程。两块
XIAO ESP32C3 烧录同一个固件，继续复用现有车内模块的 BLE 唤醒广播、
ESP-NOW 配对和会话协议。

## 待机路径

```text
首次上电且未配对
  -> 等待车内 ESP-NOW 配对并保存车内 MAC
  -> 关闭 Wi-Fi/ESP-NOW
  -> NimBLE 常驻、低占空比被动扫描
  -> 主任务永久阻塞
  -> FreeRTOS tickless idle 自动进入 Light-sleep

收到有效 BLE 唤醒广播
  -> 校验车内 MAC、session_id 和校验和
  -> 关闭 NimBLE
  -> 开启 ESP-NOW，完成 WAKE_ACK / WAKE_CONFIRM
  -> 无雷达条件下发送 3 秒固定模拟距离数据
  -> 发送结束帧
  -> 关闭 Wi-Fi/ESP-NOW并恢复 BLE 待机
```

扫描间隔由 BLE 控制器调度，不再每 3 秒重启芯片或重新初始化 NimBLE。
`sdkconfig.defaults` 已启用：

- 板载 32.768 kHz 晶振作为 RTC 和 BLE 低功耗时钟；
- BLE controller modem-sleep；
- 动态频率调节、FreeRTOS tickless idle 和自动 Light-sleep；
- Light-sleep 时关闭主晶振；
- PHY MAC/baseband 与 Flash 的睡眠掉电；
- 仅保留本工程使用的 Arduino WiFi、Network 和 Preferences 库。

## 构建环境

| PlatformIO 环境 | 扫描间隔 | 扫描窗口 | 用途 |
| --- | ---: | ---: | --- |
| `xiao_3s_30ms` | 3 s | 30 ms | 第一轮可靠性/功耗基线 |
| `xiao_3s_20ms` | 3 s | 20 ms | 降低扫描占空比 |
| `xiao_3s_10ms` | 3 s | 10 ms | 激进低功耗对照 |
| `xiao_5s_10ms` | 5 s | 10 ms | 更低功耗/更高延迟对照 |
| `xiao_3s_30ms_debug` | 3 s | 30 ms | 配对和串口诊断，不用于测功耗 |

在本目录执行：

```powershell
platformio run -e xiao_3s_30ms
platformio run -e xiao_3s_30ms -t upload
```

首次联调可先烧录 `xiao_3s_30ms_debug`。确认两块板均已配对后，再烧录无日志
的测量环境。需要重新配对时，擦除整片 Flash 后重新烧录：

```powershell
platformio run -e xiao_3s_30ms -t erase
```

## 测试注意事项

1. 先确认手中 XIAO ESP32C3 的板卡版本和原理图一致，板上 `X2` 为
   32.768 kHz 晶振；没有该晶振时不要使用这份外部慢时钟配置。
2. 功耗测量时使用电池焊盘供电并断开 USB、串口和调试器。
3. 先测 `3 s / 30 ms`，验证两个节点的唤醒成功率，再依次缩短到 20 ms
   和 10 ms；不能只根据平均电流直接选择最小窗口。
4. 这份固件测的是“开发板 + BLE/ESP-NOW”的功耗，不包含后续雷达和电源
   控制电路。
5. 测量环境关闭了应用日志；调试环境的串口和日志会明显影响待机电流。

工程在顶层 `CMakeLists.txt` 中关闭了 ESP-IDF Component Manager。当前固件
只使用已安装 Arduino Core 和 ESP-IDF 自带的组件，因此不会生成
`managed_components` 或 `dependencies.lock`。以后若引入 ESP Component
Registry 中的组件，需要重新启用 Component Manager。
