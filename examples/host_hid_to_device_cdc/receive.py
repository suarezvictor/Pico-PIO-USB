import serial

if __name__ == "__main__":
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.5)
    while True:
       read_len = ser.in_waiting
       data = ser.read(read_len if read_len > 0 else 1)
       if len(data): print(data.decode('ascii'), end='', flush=True)

