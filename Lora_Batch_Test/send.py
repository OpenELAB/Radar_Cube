import serial, time, struct, threading

PORT = "COM353"
BAUD = 9600
ser = serial.Serial(PORT, BAUD, timeout=0.5)

def reader():
    """Background thread that prints incoming data as it arrives."""
    line = b""
    while True:
        if ser.in_waiting:
            line += ser.read(ser.in_waiting)
            while b'\r\n' in line:
                pkt, line = line.split(b'\r\n', 1)
                try:
                    print("<<< Received:", pkt.decode(errors='ignore'))
                except:
                    print("Non-text data:", pkt.hex('', 1))
        time.sleep(0.01)

threading.Thread(target=reader, daemon=True).start()

idx = 0

"""AT commands for communication-module configuration."""
# 1.AT+MODE=0\r\n //Enter configuration mode
# 2.AT+RFCH=18\r\n //Set to 18 channels
# 3.AT+PID=255\r\n //Set PID to 255
# 4.AT+MAMP=1\r\n //Enable wireless wake-up mode
# 5.AT+MLPWR=0\r\n //Wireless wake-up mode 0
# 6.AT+MID=17\r\n //Set wake-up ID to 17
# 7.AT+MODE=1\r\n //Exit configuration mode
ser.write(b"AT+MODE=0\r\n")
time.sleep(0.01)
ser.write(b"AT+VER\r\n")
time.sleep(0.01)
ser.write(b"AT+RFCH=18\r\n")
time.sleep(0.01)
ser.write(b"AT+PID=255\r\n")
time.sleep(0.01)
ser.write(b"AT+MAMP=2\r\n")
time.sleep(0.01)
ser.write(b"AT+MLPWR=1\r\n")
time.sleep(0.01)
ser.write(b"AT+MID=17\r\n")
time.sleep(0.01)
ser.write(b"AT+MODE=1\r\n")
time.sleep(0.01)

while True:
    time.sleep(5)
# while idx < 1000:
    # tx_us = int(time.time_ns() / 1000)
    # buf = struct.pack("<IQ", idx, tx_us)
    # ser.write(buf)
    # print(f"Sent idx={idx} tx_us={tx_us}")
    # idx += 1
    # time.sleep(2.5)
