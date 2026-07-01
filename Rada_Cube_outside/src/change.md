# Radar Cube 代码重构记录

> **涉及文件：** 10 个  
> 重构目标：修复严重 bug、消除重复定义、统一命名风格、大幅精简代码

---

## 目录

| # | 文件 | 关键词 |
|---|------|--------|
| 1 | `config.h` | 清理重复定义 |
| 2 | `pins.h` | 统一引脚 / 按键宏 |
| 3 | `protocol.h` / `protocol.cpp` | 类型安全 + 工具函数 |
| 4 | `lora.h` / `lora.cpp` | 修 merge 冲突 + 简化 NVS |
| 5 | `radar.h` / `radar.cpp` | 修 FIFO 溢出 + 校验越界 |
| 6 | `mac_match.h` / `mac_match.cpp` | 大幅精简（-70%） |
| 7 | `sensor.h` / `sensor.cpp` | 适配新按键宏 + 唤醒源检测 |
| 8 | `espnow.h` / `espnow.cpp` | ESP-NOW 通信层封装 |
| 9 | `main.ino` | 适配所有新 API |
| 10 | `test.ino` | 测试代码（已全部注释） |

---

## 1. `config.h` — 清理重复定义

- 删除重复的 `POWER_TAG` / `LORA_TAG`（之前定义了两遍）
- 删除放错位置的 `GPIO_CE_ACTIVE_LEVEL` / `GPIO_CE_INACTIVE_LEVEL`（与 `pins.h` 冲突）
- 按功能分区加注释，结构更清晰：
  - LED / 蜂鸣器 / 电池 / Lora / 工作模式 / 按键 / 距离报警阈值 / 日志标签

---

## 2. `pins.h` — 统一引脚 / 按键宏

- 删除重复的 `extern HardwareSerial&` 声明（出现了两遍）
- 删除未使用的 `GPIO_ACTIVE_LEVEL` / `GPIO_INACTIVE_LEVEL`
- 合并 **8 个按键宏**（`USER_BUTTON_PRESSED` / `DEV_BUTTON_PRESSED` / `_ACTIVE_LEVEL` / `_INACTIVE_LEVEL`）为 **2 个**：`BUTTON_PRESSED` + `BUTTON_RELEASED`
- 重命名 `GPIO_CE_ACTIVE_LEVEL` -> `LORA_CE_ACTIVE`（语义更明确）
- 删除 `MASTER_MAC_ADDR_EXIST` 等布尔常量（只是 `true`/`false` 的别名）
- 加入编译期互斥检查：`INSIDE` 和 `OUTSIDE` 必须且只能定义其一

---

## 3. `protocol.h` / `protocol.cpp` — 类型安全 + 工具函数

- `typedef enum` -> `enum frame_type_t : uint8_t`（确保与结构体字段尺寸一致）
- 帧结构从 14 字节简化为 **8 字节**（`static_assert` 保证）
- 枚举值加统一前缀：`MASTER_MATCH_FRAME` -> `FRAME_MASTER_MATCH`
- `crc` -> `checksum`（实际是累加和，不是真正的 CRC）
- 删除 5 个无意义的 `#define`（`FRAME_DATA_LEN` / `MAC_MATCH_DATA` 等都是常量 0 或 6）
- **新增** `frame_build()` — 一行构建帧，自动填充 `reserve` + `checksum`
- **新增** `frame_validate()` — 一行校验帧（长度 + 帧头 + 类型 + 校验）

---

## 4. `lora.h` / `lora.cpp` — 修 merge 冲突 + 简化 NVS

### Bug 修复

- **严重 bug：** 文件有 merge 冲突残留 — 两个 `private:` 区、两个 `Preferences` 成员、`flag_ifconfig()` 里面嵌套了构造函数体

### 重构内容

- 4 个 NVS 函数 -> 2 个 private helper（`isConfigured()` + `setConfigured()`）+ 1 个 public `clearConfigFlag()`
- API 重命名：

  | 旧名 | 新名 |
  |------|------|
  | `lora_init` | `init()` |
  | `lora_setting` | `configure()` |
  | `at_send_wait_reponse` | `sendAT()` |
  | `wireless_wake_up` | `sendWakeFrame()` |
  | `lora_config` | `setup()` = `init()` + `configure()` |

- `sendWakeFrame()` 用 `frame_build()` 替代原来 8 行手动赋值
- 车内模块额外维护 Lora 睡眠/唤醒命令序列

---

## 5. `radar.h` / `radar.cpp` — 修 FIFO 溢出 + 校验越界

### Bug 修复

- **FIFO 溢出：** `head`/`tail` 原为 `volatile uint8_t`，但 `FIFO_SIZE = 512`（`uint8_t` 最大值 255），**改为 `volatile uint16_t`**
- **校验越界：** `verifyChecksum(raw, len=12)` 中 `raw[len]` 访问第 13 字节，循环 `i < len-1` 只累加了 11 字节。现在明确写死「前 12 字节之和 == raw[12]」

### 重构内容

- FIFO 和 parse 函数设为 `private`（外部不需要直接调用）
- 状态机枚举值重命名：`HUNT_AA` -> `WAIT_AA`，更直观
- 串口中断回调使用静态单例转发模式

---

## 6. `mac_match.h` / `mac_match.cpp` — 大幅精简

> **412 行 -> 130 行，减少约 70%**

- 7 个 NVS 方法 -> 4 个：`hasPeerMac()` / `loadPeerMac()` / `savePeerMac()` / `clearPeerMac()`
- 8 个回调函数 -> 2 个（1 个 `static` 转发 + 1 个实例方法）
- `_slave_mac` / `_master_mac` 统一为 `_peer_mac`
- `slave_mac_flag` / `master_mac_flag` 统一为 `volatile bool _matched`
- 配对流程 `pair()` 内部自带 `init()` + 注册回调 + 超时控制，调用方只需一行
- 构造函数接收 `EspNowManager&` 引用，解耦通信层

---

## 7. `sensor.h` / `sensor.cpp` — 适配新按键宏 + 唤醒源检测

- `USER_BUTTON_INACTIVE_LEVEL` -> `BUTTON_RELEASED`
- `USER_BUTTON_ACTIVE_LEVEL` / `DEV_BUTTON_ACTIVE_LEVEL` -> `BUTTON_PRESSED`
- 新增 `WakeupSource` 枚举：`WAKEUP_POWER_ON` / `WAKEUP_USER_BUTTON` / `WAKEUP_DEV_BUTTON` / `WAKEUP_BOTH_BUTTONS` / `WAKEUP_LORA`
- 新增 `PowerManager::detectWakeupSource()` — 基于 ESP-IDF API + GPIO 电平自动判断唤醒原因
  - C3 使用 `ESP_SLEEP_WAKEUP_GPIO`（GPIO 唤醒，读电平区分按键）
  - C6 使用 `ESP_SLEEP_WAKEUP_EXT1`（EXT1 唤醒，bitmask 区分按键）+ GPIO 唤醒（Lora）

---

## 8. `espnow.h` / `espnow.cpp` — ESP-NOW 通信层封装

- 纯通信层，不含业务逻辑
- 接收模型：回调写入单帧 buffer + 标志位，主循环轮询读取（只保留最新一帧）
- API：`init()` / `deinit()` / `addPeer()` / `delPeer()` / `send()` / `recvStart()` / `read()` / `recvStop()`
- 幂等初始化（重复调用 `init()` 自动跳过）
- 静态单例回调转发模式

---

## 9. `main.ino` — 适配所有新 API

- 所有旧 API 调用替换为新名称
- `esp_now_rece()` 回调里 3 层嵌套 `if` 替换为一行 `frame_validate()`
- `matcher.clear_mac_exist_flag()` + `matcher.clear_mac_addr()` 合并为 `matcher.clearPeerMac()`
- `mode` 类型从 `uint32_t` 改为 `enum SysMode`
- `switch` 缩进从两层减为一层
- 主流程清晰化：`setup()` -> 唤醒检测 -> 电量检测 -> 模式判断 -> 模式执行 -> 深度睡眠
- `loop()` 为空（所有逻辑在 `setup()` 中单次执行后进入深度睡眠）

---

## 10. `test.ino` — 测试代码

- 全部内容已注释，不参与编译
- 保留各模块历史测试片段（LED、蜂鸣器、电池、睡眠唤醒、任务监控等），仅供开发参考

---

## 当前架构概览

```
main.ino            主入口：唤醒 -> 判断模式 -> 执行 -> 睡眠
  config.h          软件参数（阈值、超时、日志标签）
  pins.h            硬件引脚 + 电平常量（INSIDE / OUTSIDE 条件编译）
  sensor.h/cpp      LED / 蜂鸣器 / 电池 / 电源管理 / 唤醒源检测
  protocol.h/cpp    通信协议帧（8B）/ 构建 / 校验工具函数
  espnow.h/cpp      ESP-NOW 通信层（收发 + 回调 + peer 管理）
  mac_match.h/cpp   MAC 地址配对（NVS 持久化 + 配对流程）
  lora.h/cpp        Lora 模块（AT 配置 + 无线唤醒 + NVS 标志）
  radar.h/cpp       雷达模块（串口 FIFO + 帧解析状态机）[仅 OUTSIDE]
```

### 编译目标

| PlatformIO 环境 | 芯片 | 角色 | 编译宏 |
|-----------------|------|------|--------|
| `esp32_c3` | ESP32-C3 | 车内模块 (INSIDE) | `-D INSIDE` |
| `esp32_c6` | ESP32-C6 | 车外模块 (OUTSIDE) | `-D OUTSIDE` |
