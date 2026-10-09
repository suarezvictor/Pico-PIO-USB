import serial
import struct
import time
import sys

sys.path.insert(0, "hid-tools")
from hidtools.hid import ReportDescriptor

STRUCT_FORMAT = '<4B3H'
STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)
assert STRUCT_SIZE == 10;
MAGIC = 0x03 #matches TUSB_CLASS_HID

rdesc = dict()

def run(ser):
	global rdesc

	ser.write(b'\x00') #trigger next read
	ser.flush()

	while not ser.in_waiting:
		pass

	data = ser.read(1)
	if data[0] != MAGIC: #usually 0
		return False

	data = data + ser.read(STRUCT_SIZE-1)

	if len(data) != STRUCT_SIZE:
		print("MISSING DATA", len(data), data)
		return False
	
	magic, evt, addr, instance, vid, pid, length = struct.unpack(STRUCT_FORMAT, data)
	payload = ser.read(length)

	header = (
		f"Magic {magic:02x} " 
		f"EVENT type {evt:02x}, " 
		f"addr {addr:02x}, " 
		f"instance {instance:02x}, " 
		f"[{vid:04x}:{pid:04x}] " 
		f"payload len {length:04x}:" )

	if evt == 0:
		try:
			print(f"ATTACHED device addr {addr:02x}, instance {instance:02x}, [{vid:04x}:{pid:04x}]")
			rdesc.setdefault(addr, {})[instance] = ReportDescriptor.from_bytes(payload)
			rdesc[addr][instance].dump()
		except:
			print(header, payload.hex(' '))
			print("DESCRPITION REPORT ERROR", e)

	try:
		r = rdesc[addr][instance]
		if evt == 2 and len(payload):
			report = r.format_report(payload)
			if report is None:
				print("REPORT ERROR.", header, payload.hex(' '))
			else:
				print(report)

		if evt == 1:
			print(f"DETACHED device addr {addr:02x}, instance {instance:02x}, [{vid:04x}:{pid:04x}]")
			rdesc[addr].pop(instance)
			if not rdesc[addr]: rdesc.pop(addr)

	except:
		print(f"no descriptor for addr {addr:02x}, instance {instance:02x}")

	return True		

if __name__ == "__main__":
	ser = serial.Serial('/dev/ttyUSB0', 230400, timeout=.1)
	while True:
		if run(ser): continue
		time.sleep(1/60) #simulate a MCU processing delay

