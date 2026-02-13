import serial
import sys
import csv
import re



PORT = 'COM377'
BAUD = 115200
CSV_FILE = 'esp-now-lora-log.csv'



# 预编译正则
SEND_RE = re.compile(r"send_count: (\d+)", re.I)
RECV_RE = re.compile(r"ESP-NOW Received: (\d+)", re.I)
CONSUM_RE = re.compile(r"consum time: (\d+)", re.I)
TIMEOUT_RE = re.compile(r"Time out count: (\d+)", re.I)

class Parser:
    def __init__(self, writer):
        self.writer = writer
        self.reset()

    def reset(self):
        self.send_count = None
        self.recv_val = None
        self.consum_time = None
    
    def feed(self, line):

        m = TIMEOUT_RE.search(line)
        if m and self.send_count is not None:
            self.writer.writerow([self.send_count, "", "", m.group(1)])
            self.reset()
            return
        
        m = SEND_RE.search(line)
        if m:
            self.send_count = m.group(1)
            return
        
        m = RECV_RE.search(line)
        if m:
            self.recv_val = m.group(1)
            return

        m = CONSUM_RE.search(line)
        if m and self.send_count is not None and self.recv_val is not None:
            self.writer.writerow([self.send_count, self.recv_val, m.group(1), ""])
            self.reset()
            return


def main():
    csv_f = open(CSV_FILE, 'w', newline='', buffering=1)
    writer = csv.writer(csv_f)
    writer.writerow(["send_count", "received", "consum_time", "timeout_count"])
    parser = Parser(writer)

    try:
        ser = serial.Serial(PORT, BAUD, timeout=None)
        print(f"已打开{PORT}@{BAUD}, 开始打印日志 ......\n")
    except serial.SerialException as e:
        print("串口打开失败：", e)
        sys.exit(1)
    
    try:
        while True:
            line = ser.readline().decode(errors='ignore').strip()
            if(line):
                print(line)
                parser.feed(line)
    except KeyboardInterrupt:
        print("用户中断")
    finally:
        csv_f.close()
        ser.close()

if __name__ == '__main__':
    main()