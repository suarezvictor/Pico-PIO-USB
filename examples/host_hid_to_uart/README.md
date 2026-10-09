# USB HID to UART bridge

This firmware allows a MCU with no USB HOST capability to access USB HID device by just using an UART. "HID" stands for _"Human Interface Device"_.  

It's based on the USB host library based on PIO from the RP2040 and compatibles, integrated into TinyUSB library.  
It supports all USB HID devices, since the implementation is generic with a design to support any device.

## Protocol and architecture

The consumer MCU just sends a zero character to request information about the connected USB HID device. It generated when a USB device is attached, detached, or has new information (a keyboard press, mouse movement, touch in a touchscreen, etc). Note the requirement of 230400 baudrate needed for some devices.

The firmware continuosly polls the USB device for USB reports. Those can be device attached (code 0), detached (code 1), or general HID reports (code 2). For example, the case of a general ID report for a touch screen, as decoded by the [receive.py](receive.py) script:

```
Magic 03 EVENT type 02, time 7096ms, addr 01, instance 00, [222a:0001] payload len 0040: 04 40 49 00 20 30 00  [...] 
```

As seen there's a header: Magic byte, event type byte, timestamp (16-bit in ms), USB address byte, USB instance byte, VID (16-bit), PID (16-bit), payload length (16-bit) followed by the payload (the HID data report since type is 2, 64 bytes in this case since all is reported in hex). Note the initial value 04 corresponding to touch data.

_Update_: now the reports are parsed with [hid-tools](https://gitlab.freedesktop.org/libevdev/hid-tools), so report is formatted. See for example a mouse report:  
```
586ms:  Button: 1  0  0  0  0  0  0  0 | # | X:    0 | Y:    1 | Wheel:    0 | AC Pan:    0 
```


The full USB descriptor is reported with event type 0, so a smart receiver like `hid-tools` can decode the data report exactly. Alternatively, the descriptor report can be ignored for known devices, since the data report always includes the VID and PID fields.

## Queues

The implementation make intensive uses of queues. A USB host polls the device with a 1ms interval, and a touchscreen reports at a rate of about 250Hz. In the other hand, even a fast MCU may poll for USB events at 60Hz or a at lower rate if for example just expecting keystrokes. For that reason, all reports (data annd others) are enquequed until the consumer ask for them. 

But internal RAM of the Rpi Pico is not large, and queues can get eventually full. So what to do in that case? One option is just to discard the new reports, but this is certainly not good: for example if you are moving a mouse, you won't know the end location. Another option is to discard the older reports, but this also has problems: you may lose for example that a mouse button was pressed. You can implement more logic to just discard mouse movements, but then you're in need of knowing about your HID devices and their protocols.

The solution for this is as follows: let the device handle the buffer overflow by *pausing data report request*, so the logic is passed to the device, which can for example, coallesing all last mouse movements by accumulating the deltas of the last (unread) movement. This way the bridge remains agnostic to the partcular details of each HID device.
  
Then another queue is used to signal that a queue has again a free slot: this is used so in the main loop, requests can be resumed.
  
It was realized in practice that a queue of 64 reports is enough for a demanding device (a touchscreen reporting at about 250Hz with 64 byte payloads). It holds about 250ms of data without overflow. To check that, a signal is reported for overflow: it's connected to GPIO 25 which is the LED of original manufacturer board.
  
All HID devices so far were working ok with no buffer overflow or stalls, so suitable for realtime operation.


## Caveats

The original intention was to use I2C as the protocol, but then it was realized that implementing I2C in the RP2040 needs to process one byte at a time in a interrupt handler, which require DMA if packets are long (like a touchscreen device descriptor of 850 bytes), and the DMA is faulty both in the RP2040 and the newer version (RP2350 family). A bit rate of 100KHz is too slow for certain HID devices, and 400Khz is too demanding without DMA. Considering all that complications and how easy is for a MCU to access via serial, the I2C route was posponed.


## Sources

The implementation is based on the [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) which supports USB host up to full-speed. The base project is `host_hid_to_device_cdc` with the USB device  feature removed. The maximum size of the enumeration was increased from 256 to 1024 to accomodate complex devices like touch panels integrated into displays.
  
Changes can be seen in the commit history [here](https://github.com/suarezvictor/Pico-PIO-USB/commits/main/?author=suarezvictor).
  

