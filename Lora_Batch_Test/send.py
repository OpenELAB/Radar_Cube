import serial, time, struct

PORT = "COM3"
BAUD = 9600
ser = serial.Serial(PORT, BAUD)


idx = 0
while True:
    tx_us = int(time.time_ns() / 1000)
    buf = struct.pack("<IQ", idx, tx_us)
    ser.write(buf)
    print(f"Sent idx={idx} tx_us={tx_us}")
    idx += 1
    time.sleep(5)