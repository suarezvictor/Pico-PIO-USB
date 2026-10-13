// Copyright (c) 2026 Victor Suarez Rovere <suarezvictor@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-only   

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>
#include <inttypes.h>

#include <memory>
#include <map>
#include <stdexcept>

#include "HIDReportDescriptor.h"
#include "../src/HIDUtils.h"

typedef struct __attribute__((packed))
{
	  uint8_t magic, evt;
	  uint16_t timestamp;
	  uint8_t addr, instance;
	  uint16_t vid, pid, len;
} report_header_t;

#define STRUCT_SIZE sizeof(report_header_t)
#define MAGIC       0x03

static int serial_open(const char *device, speed_t baudrate)
{
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror("open");
        exit(1);
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("tcgetattr");
        close(fd);
        exit(1);
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, baudrate);
    cfsetospeed(&tty, baudrate);

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

static ssize_t read_buf(int fd, uint8_t *buf, size_t n)
{
    size_t received = 0;

    while (received < n)
    {
        ssize_t r = read(fd, buf + received, n - received);
        if (r <= 0)
        	return r;

        received += (size_t)r;
    }

    return (ssize_t)received;
}

static std::map<uint8_t, std::map<uint8_t, std::shared_ptr<HIDReportDescriptor>>> descriptors;

bool parse_data_report(std::shared_ptr<HIDReportDescriptor> desc, uint8_t *data, uint16_t datalen)
{
    bool found = false;
    const uint32_t total_bits = static_cast<uint32_t>(datalen) * 8;
    
    const std::vector<HIDIOReport>& reports = desc->get_reports();

    for (uint32_t i = 0; i < reports.size(); i++) //usually just 1
    {
        const auto &report = reports[i];
        int block = 0;

        for (const auto &ioblock : report.inputs) //usually just 1
        {
            size_t bit_offset = 0;

            for (const auto &input : ioblock.data)
            {
            	size_t read_offset = bit_offset;
                uint32_t value = HIDUtils::read_bits_le(data, read_offset, input.size);

                bit_offset += input.size;
                if (bit_offset > total_bits)
                    break; // Out of range

                if (input.type == HIDIOType::Padding)
					continue; // skip

                if (input.type == HIDIOType::ReportId)
                {
					if(value == input.id) continue;
					break; // Not the correct report id
				}
					
				const char *typname = "Unknown";
				switch(input.type)
				{
					case HIDIOType::Key: typname = "Key"; break;
					case HIDIOType::X: typname = "X"; break;
					case HIDIOType::Y: typname = "Y"; break;
					case HIDIOType::Button: typname = "Button"; break;
					case HIDIOType::Wheel: typname = "Wheel"; break;
					case HIDIOType::Consumer: typname = "Consumer"; break;
					case HIDIOType::TipSwitch: typname = "TipSwitch"; break;
					case HIDIOType::ContactId: typname = "ContactId"; break;
					case HIDIOType::ScanTime: typname = "ScanTime"; break;
					case HIDIOType::ContactCount: typname = "ContactCount"; break;
				}

                printf("report %d, block %d, type %s (%d), id %d, offset %lu, size %u, value %lu (%" PRId32 ")\n",
                	i, block, typname, input.type, input.id, read_offset, input.size, value, (int32_t) HIDUtils::sign_extend(value, input.size));

                found = true;
            }
            ++block;
        }
    }

    return found;
}

static bool run(int fd)
{
    // Trigger next read
    uint8_t trigger = 0x00;
    write(fd, &trigger, 1);

    // Wait until at least one byte is available.
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    struct timeval tv = {.tv_sec = 0, .tv_usec = 100000 };
    if (select(fd + 1, &readfds, NULL, NULL, &tv) <= 0)
        return false;

    union
    {
	    report_header_t header;
	    uint8_t data[STRUCT_SIZE];
	} u;

    ssize_t n = read_buf(fd, u.data, 1);
    if (n != 1)
        return false;

    if (u.data[0] != MAGIC)
        return false;

    n = read_buf(fd, u.data + 1, STRUCT_SIZE - 1);
    if (n != STRUCT_SIZE - 1)
    {
        printf("* %d bytes\n", n + 1);
        return true;
    }
    
    uint8_t magic    = u.header.magic;
    uint8_t evt      = u.header.evt;
    uint16_t timestamp      = u.header.timestamp;
    uint8_t addr     = u.header.addr;
    uint8_t instance = u.header.instance;

    uint16_t vid = u.header.vid;
    uint16_t pid = u.header.pid;
    uint16_t length = u.header.len;

    uint8_t *payload = NULL;

    if (length > 0) 
    {
        payload = (uint8_t *) malloc(length);
        if (!payload)
        {
            perror("malloc");
            exit(1);
        }

        n = read_buf(fd, payload, length);
        if (n != length)
        {
            printf("* %zd bytes (expected %u)\n", n, length);
            free(payload);
            return true;
        }
    }

    printf("%dms: Magic %02x EVENT type %02x, "
           "addr %02x, instance %02x, "
           "[%04x:%04x] payload len %04x:",
           timestamp, magic, evt, addr, instance, vid, pid, length);

    for (uint16_t i = 0; i < length; i++)
        printf(" %02x", payload[i]);

    printf("\n");
    

	switch(evt)
	{
		case 0: //device attached
		{
			auto desc = std::make_shared<HIDReportDescriptor>(payload, length);
			if(desc != NULL)
			{
				printf("Parsed %lu HID fields\n", desc->get_reports().size());
				descriptors[addr][instance] = desc;
			}
			break;
		}

		case 1: //device detached
			try
			{
				auto& descs = descriptors.at(addr);
				descs.erase(instance);
				if (descs.empty()) descriptors.erase(addr);
				printf("DETACHED device addr %02x, instance %02x\n", addr, instance);
			}
			catch(const std::out_of_range&)
			{
				printf("no descriptor to detach for addr %02x, instance %02x\n", addr, instance);
			}
			break;
			
		case 2: //data report
			if (length != 0)
			try
			{
				auto desc = descriptors.at(addr).at(instance);
				parse_data_report(desc, payload, length);
			}
			catch(const std::out_of_range&)
			{
				printf("no descriptor for addr %02x, instance %02x\n", addr, instance);
			}
			break;
		default:
			break;
	}
 

	free(payload);
    return true;
}

int main(void)
{
    int fd = serial_open("/dev/ttyUSB0", B230400);

    if (fd < 0)
    {
        perror("cannot open serial port");
        return 1;
    }

    for(;;)
    {
        if (run(fd))
            continue;

        usleep(1000000/60);
    }

    close(fd);
    return 0;
}

