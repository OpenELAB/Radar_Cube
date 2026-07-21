# Test results

## 1. Over-the-air wake-up mode 2 + the sender delays sending data for 5 seconds and tests 1000 times, MAMP=1 mode
**Regardless of serious packet loss and long wake-up time**
```
Wake-up success rate: 47.4%
Maximum consecutive packet losses: 10
Maximum wake-up time: 52,400 ms
Minimum wake-up time: 2,400 ms
```

## 2. Over-the-air wake-up mode 2 + sender delay 5s to send data test 1000 times, MAMP=2 mode
```
Wake-up success rate: 98.7%
Maximum consecutive packet losses: 2
Maximum wake-up time: 12,300 ms
Minimum wake-up time: 2,300 ms
```

## 3. Over-the-air wake-up mode 2 + sender delay 0.5s to send data test 100 times, MAMP=2 mode
**Not considered, serious packet loss, long wake-up time**
```
Wake-up success rate: 30%
Maximum consecutive packet losses: 4
Maximum wake-up time: 21,000 ms
Minimum wake-up time: 2,300 ms
```

## 4. Over-the-air wake-up mode 2 + sender delay 1s to send data test 100 times, MAMP=2 mode
**Regardless of serious packet loss and long wake-up time**
```
Wake-up success rate: 51%
Maximum consecutive packet losses: 4
Maximum wake-up time: 21,000 ms
Minimum wake-up time: 2,300 ms
```

## 5. Over-the-air wake-up mode 2 + sender delay 2.5s to send data test 100 times, MAMP=2 mode
```
Wake-up success rate: 98.5%
Maximum consecutive packet losses: 1
Maximum wake-up time: 4,800 ms
Minimum wake-up time: 2,300 ms
```

## 6. Over-the-air wake-up mode 1 + sender delayed sending data test 1000 times, MAMP=2 mode
```

```

# Test using ESP32C3+lora
## Power on and configure to enter wireless wake-up mode
**About 450ms**
```
The communication module retains its settings after power loss. Configure it at first boot and store an NVS flag. Skip configuration on later boots when the flag is present.

At first boot, configure normal operation and over-the-air wake-up:

  "AT+MODE=0",    // Enter configuration mode
  "AT+RFBR=6000", // Set normal-mode air data rate to 6000 bps
  "AT+LPWR=0",    // Disable deep sleep
  "AT+RFCH=18",   // Set the 433 MHz wake-up channel
  "AT+PID=255",   // Set PID to 255
  "AT+MAMP=2",    // Enable over-the-air wake-up
  "AT+MLPWR=2",   // Enable wake-up mode 2
  "AT+MID=17",    // Set wake-up ID to 17
  "AT+MODE=1"     // Exit configuration mode
```

## Switch mode
### Normal mode
**About 134ms**
```
  AT+MODE=0
  AT+MAMP=0
  AT+MODE=1

```
### Wireless wake-up mode
**About 131ms**
```
  AT+MODE=0
  AT+MAMP=2
  AT+MODE=1
```
