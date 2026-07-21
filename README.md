# ESP-NOW folder
```
The code uses standard ESP-NOW only; sleep and low-power features have not been added.
```
## Lora+ESP-NOW
```
lora_recv_test receives data and wakes a sleeping ESP32.
lora_send_test sends data; a button wakes the ESP32, which sends data to the LoRa module over serial.
Rada_inside is the in-vehicle controller module. A button wakes its ESP32, which then uses LoRa over-the-air wake-up mode to wake the two deeply sleeping sensor nodes at the rear of the vehicle.
```
