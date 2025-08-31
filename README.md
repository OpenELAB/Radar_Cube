# Radar_Cube
无线倒车雷达模块
- Cude - 车内控制模块
- Sensor - 车后雷达传感器模块

## 方案对比
- 主控esp32c3自带wifi和蓝牙无线通信模块，初期先不考虑外置nrf模块的方案
- 功耗对比：wifi和esp now都是基于wifi的通信，功耗比较高，蓝牙因为有ble版本，功耗会明显低于基于wifi的方案。所以作为初期的视线方案，选择ble作为通信方案

## 当前方案：
- 主控：esp32c3
- 通信：板载BLE模块
- 低功耗思路：light sleep轻度睡眠模式下，Sensor模块进行周期性ble信号扫描，监听到来自Cube的信号就唤醒然后开始采集雷达数据传给Cube

## demo 思路，先在不考虑功耗的情况下跑通两种ble通信方式
- 测试蓝牙广播和接收： cube设备在按钮按下后，不断周期性发出广播数据，sensor设备监听广播，接收到cube特定的的广播数据之后切换到正常通信模式
- 测试connect之后进行正常通信：

- Arduino IDE -> File -> example -> BLE 里面有一些案例

| 场景             | 示例                                      |
|------------------|-------------------------------------------|
| 周期广播（Cube） | `BLE5_periodic_advertising`               |
| 周期广播接收（Sensor）     | `BLE5_periodic_sync`                      |
| 建立双向连接     | `Client`（Sensor） + `Server`（Cube） （或者UART示例，更简单，可以作为entrypoint，之后再用client server）     |

## device_a device_b 是ai生成的demo代码，没有跑过，可能有错误，主要看一下程序逻辑

## 后续规划
1. 传感器雷达数据采集和处理
2. 两个设备通信内容设计，可能需要加密或者其他方式保证多套设备不冲突
3. 低功耗设计，先看一下加上轻度睡眠模式后功耗如何，再看是否需要继续优化
    - 用 scan_interval/window duty-cycle，C3 在 light sleep 下可实现“低功耗监听”，扫到广播才真正唤醒 CPU。
    - 如果触发端连续发 1–2 s 广播，漏接概率几乎可以忽略（通过非整数周期 + 抖动机制消除对齐问题）。

## 无线睡眠唤醒esp官方示例代码
- wifi：https://github.com/espressif/esp-idf/tree/v5.5/examples/wifi/power_save
- ble： https://github.com/espressif/esp-idf/tree/v5.5/examples/bluetooth/nimble/power_save

## 8月31更新
- 蓝牙正常建立连接的过程包含了一开始的server广播和client监听，也就是说不需要periodic_advertising和periodic_sync，直接server等待client连接就行