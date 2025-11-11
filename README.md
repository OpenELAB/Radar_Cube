# ESP-NOW文件夹
```
代码里只有普通的esp-now没有增加睡眠低功耗
```
## Lora+ESP-NOW
```
lora_recv_test用作接收数据，唤醒睡眠的ESP32
lora_send_test用作发送数据，按键唤醒ESP32通过串口发送数据给Lora模组
Rada_inside车内主机模块，用于按键唤醒ESP32，在通过Lora的空中唤醒模式来唤醒车尾的两个深度睡眠的从机
```