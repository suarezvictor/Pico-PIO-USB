import serial
import struct
import time

STRUCT_FORMAT = '<4B3H'
STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)
assert STRUCT_SIZE == 10;
MAGIC = 0x90


def run(ser):
	ser.write(b'\x00') #trigger next read
	ser.flush()

	while not ser.in_waiting:
		pass

	data = ser.read(1)
	if data[0] != MAGIC:
		return False

	data = data + ser.read(STRUCT_SIZE-1)

	if len(data) == STRUCT_SIZE:
		magic, evt, addr, instance, vid, pid, length = struct.unpack(STRUCT_FORMAT, data)
		data = ser.read(length)

		header = (
			f"Magic {magic:02x} " 
			f"EVENT type {evt:02x}, " 
			f"addr {addr:02x}, " 
			f"instance {instance:02x}, " 
			f"[{vid:04x}:{pid:04x}] " 
			f"payload len {length:04x}:" )

		print(header, data.hex(' '))
	else:
		print("*", len(data), data)
	
	return True		

if __name__ == "__main__":
	ser = serial.Serial('/dev/ttyUSB0', 230400, timeout=.1)
	while True:
		if run(ser): continue
		time.sleep(1/60) #simulate a MCU processing delay

