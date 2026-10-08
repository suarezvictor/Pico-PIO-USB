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
#include "pico/util/queue.h"

#include "host/hcd.h"
#include "pio_usb.h"
#include "tusb.h"
#include "hardware/uart.h"

#define UART_ID		uart1
#define BAUD_RATE	230400
#define UART_TX_PIN	20
#define UART_RX_PIN	21

#define REPORT_QUEUE_SIZE	64 //64 allows about 250ms stalls in consumer (touchscreen device)
#define LED_PIN	25

//log functions by USB CDC
#define tud_cdc_write_str(...)
#define tud_cdc_write(...)

typedef struct __attribute__((packed))
{
	  uint8_t magic, evt, addr, instance;
	  uint16_t vid, pid, len;
} report_header_t;

typedef struct {
    report_header_t header;
    uint8_t *report;
} hid_report_entry_t;

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

static queue_t report_queues[CFG_TUH_DEVICE_MAX][CFG_TUH_HID];

typedef struct {
    uint8_t dev_addr;
    uint8_t instance;
} hid_dev_id_t;

static queue_t resume_queue;
void on_uart1_rx();
bool drain_report_queues(void);

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

  //configure led
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);

  //configure uart
  uart_init(UART_ID, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
  
  uart_set_irq_enables(UART_ID, true, false);
  irq_set_exclusive_handler(UART1_IRQ, on_uart1_rx);
  irq_set_enabled(UART1_IRQ, true);

  for (uint8_t dev_addr = 1; dev_addr <= CFG_TUH_DEVICE_MAX; ++dev_addr)
  {
      for (uint8_t instance = 0; instance < CFG_TUH_HID; ++instance)
      {
		if(queue_init(&report_queues[dev_addr-1][instance], sizeof(hid_report_entry_t), REPORT_QUEUE_SIZE))
			continue;
		panic("Error: cannot allocate queues\r\n");
      }
  }
  queue_init(&resume_queue, sizeof(hid_dev_id_t), CFG_TUH_DEVICE_MAX*CFG_TUH_HID);

  while (true) {
    tud_task(); // tinyusb device task
    
	hid_dev_id_t dev_id;
    while (queue_try_remove(&resume_queue, &dev_id))
    {
        if (tuh_hid_mounted(dev_id.dev_addr, dev_id.instance))
        {
            tuh_hid_receive_report(dev_id.dev_addr, dev_id.instance);
        }
	    gpio_put(LED_PIN, false);
    }
    
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

bool drain_report_queues(void)
{
  hid_report_entry_t entry;
  for (uint8_t dev_addr = 1; dev_addr <= CFG_TUH_DEVICE_MAX; ++dev_addr)
  {
      for (uint8_t instance = 0; instance < CFG_TUH_HID; ++instance)
      {
		  queue_t *q = &report_queues[dev_addr-1][instance];
		  bool was_full = queue_is_full(q);

    		if (queue_try_remove(q, &entry))
    		{
				uart_write_blocking(UART_ID, (uint8_t*) &entry.header, sizeof(entry.header));
				uart_write_blocking(UART_ID, entry.report, entry.header.len);
				free(entry.report);

				if (was_full)
				{
					hid_dev_id_t dev_id = { entry.header.addr, entry.header.instance };
					queue_try_add(&resume_queue, &dev_id);
				    tud_cdc_write_str("Error: buffer was full in tud_cdc_rx_cb\r\n");
				}

				return true;
			}
		}
    }

    return false;
}

void on_uart1_rx(void)
{
	if (uart_is_readable(UART_ID))
	{
        (void)uart_getc(UART_ID);

		if(!drain_report_queues())
			uart_write_blocking(UART_ID, "\0", 1);
		uart_tx_wait_blocking(UART_ID);
	}
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+
bool dump_report(hcd_eventid_t evt, uint8_t addr, uint8_t instance, uint8_t const* report, uint16_t len);

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{

	if(!dump_report(HCD_EVENT_DEVICE_ATTACH, dev_addr, instance, desc_report, desc_len))
	  return;


  // tuh_hid_report_received_cb() will be invoked when report is available
	tuh_hid_receive_report(dev_addr, instance);
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  dump_report(HCD_EVENT_DEVICE_REMOVE, dev_addr, instance, NULL, 0);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
	if(!dump_report(HCD_EVENT_XFER_COMPLETE, dev_addr, instance, report, len))
	  return;

  tuh_hid_receive_report(dev_addr, instance);
}

bool dump_report(hcd_eventid_t evt, uint8_t addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
	bool added = false;
	
	uint16_t vid, pid;
	tuh_vid_pid_get(addr, &vid, &pid);
	hid_report_entry_t entry = { .header = { 0x90, evt, addr, instance, vid, pid, len }};

	char tempbuf[256];
	int count;
	count = sprintf(tempbuf, "Magic %02x EVENT type %02x, addr %02x, instance %02x, [%04x:%04x] payload len %04x: ",
		entry.header.magic, evt, addr, instance, vid, pid, len);
	tud_cdc_write(tempbuf, count);
	
	{
	  entry.report = len > 0 ? (uint8_t *) malloc(len) : NULL;
	  if(entry.report != NULL || len == 0)
	  {
		  if(len > 0) memcpy(entry.report, report, len);
		  added = queue_try_add(&report_queues[addr-1][instance], &entry);
		  if(!added)
		  {
		  	free(entry.report);
		    gpio_put(LED_PIN, true);
		  }
	  }
	}
	return added;
}
