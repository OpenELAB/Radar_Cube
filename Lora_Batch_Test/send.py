import serial, time, struct, threading

PORT = "COM353"
BAUD = 9600
ser = serial.Serial(PORT, BAUD, timeout=0.5)

def reader():
    """后台线程，随时把收到的数据打印出来"""
    line = b""
    while True:
        if ser.in_waiting:
            line += ser.read(ser.in_waiting)
            while b'\r\n' in line:
                pkt, line = line.split(b'\r\n', 1)
                try:
                    print("<<<收到：", pkt.decode(errors='ignore'))
                except:
                    print("非文本数据：", pkt.hex('', 1))
        time.sleep(0.05)

threading.Thread(target=reader, daemon=True).start()

idx = 0

"""通信模组配置AT指令"""
# 1.AT+MODE=0\r\n //进入配置模式
# 2.AT+RFCH=18\r\n //设置为18通道 
# 3.AT+PID=255\r\n //设置PID为255
# 4.AT+MAMP=1\r\n //使能无线唤醒模式
# 5.AT+MLPWR=0\r\n //无线唤醒模式0
# 6.AT+MID=17\r\n //设置唤醒ID为17
# 7.AT+MODE=1\r\n //退出配置模式
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

while True:
    tx_us = int(time.time_ns() / 1000)
    buf = struct.pack("<IQ", idx, tx_us)
    ser.write(buf)
    print(f"Sent idx={idx} tx_us={tx_us}")
    idx += 1
    time.sleep(2.5)