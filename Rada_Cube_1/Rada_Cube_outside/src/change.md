# Radar Cube code reconstruction record

> **Documents involved:** 10
> Refactoring goals: fix serious bugs, eliminate duplicate definitions, unify naming styles, and greatly simplify code

---

## Directory

| # | Documents | Keywords |
|---|------|--------|
| 1 | `config.h` | Clean up duplicate definitions |
| 2 | `pins.h` | Unified pin/button macro |
| 3 | `protocol.h` / `protocol.cpp` | Type safety + utility functions |
| 4 | `lora.h` / `lora.cpp` | Fix merge conflicts + simplify NVS |
| 5 | `radar.h` / `radar.cpp` | Fix FIFO overflow + checkout |
| 6 | `mac_match.h` / `mac_match.cpp` | Drastically streamlined (-70%) |
| 7 | `sensor.h` / `sensor.cpp` | Adapt new button macro + wake-up source detection |
| 8 | `espnow.h` / `espnow.cpp` | ESP-NOW communication layer encapsulation |
| 9 | `main.ino` | Adapt to all new APIs |
| 10 | `test.ino` | Test code (all commented) |

---

## 1. `config.h` — Clean up duplicate definitions

- Delete duplicate `POWER_TAG` / `LORA_TAG` (defined twice before)
- Remove misplaced `GPIO_CE_ACTIVE_LEVEL` / `GPIO_CE_INACTIVE_LEVEL` (conflict with `pins.h`)
- Add comments according to functional divisions to make the structure clearer:
  - LED / Buzzer / Battery / Lora / Working Mode / Button / Distance Alarm Threshold / Log Tag

---

## 2. `pins.h` — unified pin/button macro

- Remove duplicate `extern HardwareSerial&` declaration (appears twice)
- Remove unused `GPIO_ACTIVE_LEVEL` / `GPIO_INACTIVE_LEVEL`
- Merge **8 key macros** (`USER_BUTTON_PRESSED` / `DEV_BUTTON_PRESSED` / `_ACTIVE_LEVEL` / `_INACTIVE_LEVEL`) into **2**: `BUTTON_PRESSED` + `BUTTON_RELEASED`
- Rename `GPIO_CE_ACTIVE_LEVEL` -> `LORA_CE_ACTIVE` (more clear semantics)
- Remove boolean constants such as `MASTER_MAC_ADDR_EXIST` (just aliases for `true`/`false`)
- Add compile-time mutual exclusion check: `INSIDE` and `OUTSIDE` must and can only define one of them

---

## 3. `protocol.h` / `protocol.cpp` — type safety + utility functions

- `typedef enum` -> `enum frame_type_t : uint8_t` (make sure it is consistent with the structure field size)
- Frame structure simplified from 14 bytes to **8 bytes** (`static_assert` guarantee)
- Add a unified prefix to the enumeration value: `MASTER_MATCH_FRAME` -> `FRAME_MASTER_MATCH`
- `crc` -> `checksum` (actually the cumulative sum, not the real CRC)
- Remove 5 meaningless `#defines` (`FRAME_DATA_LEN` / `MAC_MATCH_DATA`, etc. are all constants 0 or 6)
- **New** `frame_build()` — one line to build the frame, automatically fill in `reserve` + `checksum`
- **New** `frame_validate()` — Verify frame in one line (length + frame header + type + verification)

---

## 4. `lora.h` / `lora.cpp` — fix merge conflicts + simplify NVS

### Bug fix

- **Serious bug:** The file has merge conflict residue - two `private:` areas, two `Preferences` members, `flag_ifconfig()` has a constructor body nested inside it

### Refactoring content

- 4 NVS functions -> 2 private helpers (`isConfigured()` + `setConfigured()`) + 1 public `clearConfigFlag()`
- API rename:

  | old name | New name |
  |------|------|
  | `lora_init` | `init()` |
  | `lora_setting` | `configure()` |
  | `at_send_wait_reponse` | `sendAT()` |
  | `wireless_wake_up` | `sendWakeFrame()` |
  | `lora_config` | `setup()` = `init()` + `configure()` |

- `sendWakeFrame()` uses `frame_build()` to replace the original 8 lines of manual assignment
- Additional maintenance of in-car modules Lora sleep/wake command sequence

---

## 5. `radar.h` / `radar.cpp` — fix FIFO overflow + check out-of-bounds

### Bug fix

- **FIFO overflow:** `head`/`tail` was `volatile uint8_t`, but `FIFO_SIZE = 512` (`uint8_t` max 255), ** changed to `volatile uint16_t`**
- **Verification out of bounds:** In `verifyChecksum(raw, len=12)`, `raw[len]` accesses the 13th byte, and the loop `i < len-1` only accumulates 11 bytes. Now explicitly hardcode "sum of the first 12 bytes == raw[12]"

### Refactoring content

- FIFO and parse functions are set to `private` (no need to call directly from outside)
- Renaming of state machine enumeration values: `HUNT_AA` -> `WAIT_AA`, more intuitive
- The serial port interrupt callback uses static singleton forwarding mode

---

## 6. `mac_match.h` / `mac_match.cpp` — greatly streamlined

> **412 lines -> 130 lines, about 70% reduction**

- 7 NVS methods -> 4: `hasPeerMac()` / `loadPeerMac()` / `savePeerMac()` / `clearPeerMac()`
- 8 callback functions -> 2 (1 `static` forward + 1 instance method)
- `_slave_mac` / `_master_mac` unified into `_peer_mac`
- `slave_mac_flag` / `master_mac_flag` unified into `volatile bool _matched`
- Pairing process `pair()` comes with `init()` + registration callback + timeout control, the caller only needs one line
- The constructor receives the `EspNowManager&` reference to decouple the communication layer

---

## 7. `sensor.h` / `sensor.cpp` — Adapt new button macro + wake-up source detection

- `USER_BUTTON_INACTIVE_LEVEL` -> `BUTTON_RELEASED`
- `USER_BUTTON_ACTIVE_LEVEL` / `DEV_BUTTON_ACTIVE_LEVEL` -> `BUTTON_PRESSED`
- Added `WakeupSource` enum: `WAKEUP_POWER_ON` / `WAKEUP_USER_BUTTON` / `WAKEUP_DEV_BUTTON` / `WAKEUP_BOTH_BUTTONS` / `WAKEUP_LORA`
- Added `PowerManager::detectWakeupSource()` — automatically determine the cause of wake-up based on ESP-IDF API + GPIO level
  - C3 uses `ESP_SLEEP_WAKEUP_GPIO` (GPIO wake-up, read level distinction button)
  - C6 uses `ESP_SLEEP_WAKEUP_EXT1` (EXT1 wake-up, bitmask distinguishes keys) + GPIO wake-up (Lora)

---

## 8. `espnow.h` / `espnow.cpp` — ESP-NOW communication layer encapsulation

- Pure communication layer, no business logic
- Receiving model: callback writes a single frame buffer + flag bit, and the main loop polls to read (only the latest frame is retained)
- API：`init()` / `deinit()` / `addPeer()` / `delPeer()` / `send()` / `recvStart()` / `read()` / `recvStop()`
- Idempotent initialization (repeated calls to `init()` are automatically skipped)
- Static singleton callback forwarding mode

---

## 9. `main.ino` — adapt to all new APIs

- All old API calls replaced with new names
- Replace the 3 levels of nested `if` in the `esp_now_rece()` callback with one line of `frame_validate()`
- `matcher.clear_mac_exist_flag()` + `matcher.clear_mac_addr()` merged into `matcher.clearPeerMac()`
- `mode` type changed from `uint32_t` to `enum SysMode`
- `switch` reduces indentation from two levels to one
- Clarification of the main process: `setup()` -> Wake-up detection -> Power detection -> Mode judgment -> Mode execution -> Deep sleep
- `loop()` is empty (all logic enters deep sleep after a single execution in `setup()`)

---

## 10. `test.ino` — test code

- All content has been commented and does not participate in compilation
- Keep the historical test fragments of each module (LED, buzzer, battery, sleep wake-up, task monitoring, etc.) for development reference only

---

## Overview of current architecture

```
main.ino            Main entry point: wake -> select mode -> run -> sleep
  config.h          Software settings (thresholds, timeouts, log tags)
  pins.h            Hardware pins and signal levels (INSIDE / OUTSIDE)
  sensor.h/cpp      LED, buzzer, battery, power, and wake-source handling
  protocol.h/cpp    8-byte communication frames, construction, and validation
  espnow.h/cpp      ESP-NOW transport, callbacks, and peer management
  mac_match.h/cpp   MAC pairing, NVS persistence, and pairing flow
  lora.h/cpp        LoRa AT configuration, wireless wake-up, and NVS flag
  radar.h/cpp       Radar UART FIFO and frame parser (OUTSIDE only)
```

### Compilation target

| PlatformIO environment | Chip | Role | Compile macro |
|-----------------|------|------|--------|
| `esp32_c3` | ESP32-C3 | In-vehicle module (INSIDE) | `-D INSIDE` |
| `esp32_c6` | ESP32-C6 | Off-vehicle module (OUTSIDE) | `-D OUTSIDE` |
