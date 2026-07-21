import serial, struct, time, datetime, threading

PORT = "COM378"
BAUD = 9600
PCG = 12

ser = serial.Serial(PORT, BAUD, timeout=0.5)

"""AT commands for communication-module configuration."""
# 1.AT+MODE=0\r\n //Enter configuration mode
# 2.AT+RFCH=18\r\n //Set to 18 channels
# 3.AT+PID=255\r\n //Set PID to 255
# 4.AT+MAMP=1\r\n //Enable wireless wake-up mode
# 5.AT+MLPWR=0\r\n //Wireless wake-up mode 0
# 6.AT+MID=17\r\n //Set wake-up ID to 17
# 7.AT+MODE=1\r\n //Exit configuration mode
ser.write(b"AT+MODE=0\r\n")
time.sleep(1)
ser.write(b"AT+VER\r\n")
time.sleep(1)
ser.write(b"AT+RFCH=18\r\n")
time.sleep(1)
ser.write(b"AT+PID=255\r\n")
time.sleep(1)
ser.write(b"AT+MAMP=2\r\n")
time.sleep(1)
ser.write(b"AT+MLPWR=1\r\n")
time.sleep(1)
ser.write(b"AT+MID=17\r\n")
time.sleep(1)
ser.write(b"AT+MODE=1\r\n")
time.sleep(1)


ser.reset_input_buffer()

print('idx\trx\trx_us\ttx_us\tdelay_s\ttime')


while True:
    if ser.in_waiting >= PCG:
        idx, tx_us = struct.unpack('<IQ', ser.read(PCG))
        rx_us = int(time.time_ns() / 1000)
        delay = (rx_us - tx_us) / 1000000
        now = datetime.datetime.now().strftime('%M:%S.%f')[:-3]
        line = f'{idx}\t{rx_us}\t{tx_us}\t{delay}\t{now}'
        print(line)
        with open('delay_log.txt', 'a') as f:
            f.write(line + '\n')




