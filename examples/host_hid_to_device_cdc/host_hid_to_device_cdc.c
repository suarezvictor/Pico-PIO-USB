/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    sekigon-gonnoc
 * Copyright (c) 2026 Victor Suarez Rovere <suarezvictor@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// This example runs both host and device concurrently. The USB host receive
// reports from HID device and print it out over USB Device CDC interface.
// For TinyUSB roothub port0 is native usb controller, roothub port1 is
// pico-pio-usb.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "host/hcd.h"
#include "pio_usb.h"
#include "tusb.h"
#include "hardware/uart.h"

#define UART_ID		uart1
#define BAUD_RATE	115200
#define UART_TX_PIN	20
#define UART_RX_PIN	21

/*------------- MAIN -------------*/

// core1: handle host events
void core1_main() {
  sleep_ms(10);

  // Use tuh_configure() to pass pio configuration to the host stack
  // Note: tuh_configure() must be called before
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
  // port1) on core1
  tuh_init(1);

  while (true) {
    tuh_task(); // tinyusb host task
  }
}

// core0: handle device events
int main(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  sleep_ms(10);

  multicore_reset_core1();
  // all USB task run in core1
  multicore_launch_core1(core1_main);

  // init device stack on native usb (roothub port0)
  tud_init(0);

  //configure uart
  uart_init(UART_ID, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);

  while (true) {
    tud_task(); // tinyusb device task
    tud_cdc_write_flush();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device CDC
//--------------------------------------------------------------------+

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;

  char buf[64];
  uint32_t count = tud_cdc_read(buf, sizeof(buf));

  // TODO control LED on keyboard of host stack
  (void) count;
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+
void dump_report(hcd_eventid_t evt, uint8_t addr, uint8_t instance, uint8_t const* report, uint16_t len);

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{

  // Receive report from boot keyboard & mouse only
  // tuh_hid_report_received_cb() will be invoked when report is available
	if ( !tuh_hid_receive_report(dev_addr, instance) )
	{
	  tud_cdc_write_str("Error: cannot request report\r\n");
	}
	else
	  dump_report(HCD_EVENT_DEVICE_ATTACH, dev_addr, instance, desc_report, desc_len);
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  dump_report(HCD_EVENT_DEVICE_REMOVE, dev_addr, instance, NULL, 0);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    tud_cdc_write_str("Error: cannot request report\r\n");
  }
  else
    dump_report(HCD_EVENT_XFER_COMPLETE, dev_addr, instance, report, len);
}


void dump_report(hcd_eventid_t evt, uint8_t addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
	struct __attribute__((packed))
	{
	  uint8_t magic, evt, addr, instance;
	  uint16_t vid, pid, len;
	} header = { 0x90, evt, addr, instance, 0, 0, len };
	
	tuh_vid_pid_get(addr, &header.vid, &header.pid);

	char tempbuf[256];
	int count;
	count = sprintf(tempbuf, "Magic %02x EVENT type %02x, addr %02x, instance %02x, [%04x:%04x] payload len %04x: ",
		header.magic, header.evt, header.addr, header.instance, header.vid, header.pid, header.len);
	tud_cdc_write(tempbuf, count);

	uart_write_blocking(UART_ID, (uint8_t*) &header, sizeof(header));
    uart_write_blocking(UART_ID, report, len);
	uart_tx_wait_blocking(UART_ID);

	while(len--)
	{
	  //example HCD_EVENT_XFER_COMPLETE report for VID:PID 222a:0001
	  //START: 04 40 f6 05 55 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 01 ff 00 00 00 00 00 00
	  //DRAG: 04 40 2c 06 35 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 50 00 00 00 01 41 ff 00 00 00 00 00 00
	  //RELEASE: 04 00 98 05 3b 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 0e 00 00 01 01 ff 00 00 00 00 00 00

	  /*
	  this together with te description report seems to imply:
	#define TOUCH_REPORT_ID     0x04
	#define TOUCH_MAX_CONTACTS  10
	#define TOUCH_LOGICAL_MAX   16384

	typedef struct __attribute__((packed)) {
		uint8_t  contact_id : 6;   // bits 0-5
		uint8_t  tip_switch : 1;   // bit 6
		uint8_t  pad        : 1;   // bit 7
		uint16_t x;                // 0..16384
		uint16_t y;                // 0..16384
	} touch_finger_t;

	typedef struct __attribute__((packed)) {
		uint8_t        report_id;                   // 0x04
		touch_finger_t finger[TOUCH_MAX_CONTACTS];
		uint32_t       scan_time;
		uint8_t        contact_count;
		uint8_t        vendor_reserved[8];
	} touch_report_t;
	*/

	  count = sprintf(tempbuf, "%02x ", *report++);
	  tud_cdc_write(tempbuf, count);
	}

	tud_cdc_write_str("\r\n");

	switch(evt)
	{
		case HCD_EVENT_DEVICE_ATTACH:
			//850 bytes descriptor for PID:VID 222a:0001
			tud_cdc_write_str("(EVENT_DEVICE_ATTACH)\r\n\r\n");
			break;

		case HCD_EVENT_DEVICE_REMOVE:
			tud_cdc_write_str("(HCD_EVENT_DEVICE_REMOVE)\r\n\r\n");
			break;

		default:
			break;
	}

	tud_cdc_write_flush();
}
