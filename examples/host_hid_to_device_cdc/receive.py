import serial
import struct

STRUCT_FORMAT = '<4B3H'
STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)
assert STRUCT_SIZE == 10;

if __name__ == "__main__":
	ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.5)
	while True:
		data = ser.read(STRUCT_SIZE)
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

