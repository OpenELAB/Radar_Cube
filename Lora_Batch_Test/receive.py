import serial, struct, time, datetime

PORT = ""
BAUD = 9600
PCG = 12

ser = serial.Serial(PORT, BAUD)
ser.reset_input_buffer()

print('idx\trx\trx_us\ttx_us\tdelay_us\ttime')

while True:
    if ser.in_waiting >= PCG:
        idx, tx_us = struct.unpack('<IQ', ser.read(PCG))
        rx_us = int(time.time_ns() / 1000)
        delay = rx_us - tx_us
        now = datetime.datetime.now().strftime('%M:%S.%f')[:-3]
        line = f'{idx}\t{rx_us}\t{tx_us}\t{delay}\t{now}'
        print(line)
        with open('delay_log.txt', 'a') as f:
            f.write(line + '\n')




