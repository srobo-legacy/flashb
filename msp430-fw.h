/*   Copyright (C) 2008 Robert Spanton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* Routines for sending/receiving firmware to an MSP430 over I2C */
#ifndef __MSP430_FW
#define __MSP430_FW
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>

#include "i2c-dev.h"
#include "i2c.h"
#include "smbus_pec.h"
#include "elf-access.h"

#define CHUNK_SIZE 16

/* Names for the I2C commands */
enum {
	/* Read firmware from the msp430 */
	CMD_FW_VER = 0,
	/* Send a chunk to the msp430 */
	CMD_FW_CHUNK,
	/* Read the next address that the msp430 expects */
	CMD_FW_NEXT,
	/* Read the CRC of the firmware calculated on the msp430 */
	CMD_FW_CRCR,
	/* Confirm the firmware CRC, triggering switchover */
	CMD_FW_CONFIRM,

	/* Number of commands */
	NUM_COMMANDS
};

extern uint8_t commands[NUM_COMMANDS];
extern uint8_t* msp430_fw_i2c_address;

extern uint16_t msp430_fw_bottom;
extern uint16_t msp430_fw_top;

/* Read the firmware version from the device 
   Only try a few times if give_up is TRUE. 
   Return FALSE on failure.
   Result put in *ver. */
gboolean msp430_get_fw_version( int fd, uint16_t *ver, gboolean give_up );

/* Read the next address the device is expecting */
uint16_t msp430_get_next_address( int fd );

uint16_t msp430_get_next_address_once( int fd );

/* Send a 16 byte chunk of firmware to the msp430.
   Arguments:
    -     fd: The i2c device file descriptor
    - fw_ver: The firmware version
    -   addr: The chunk address
    -  chunk: Pointer to the 16 byte chunk of data */
void msp430_send_block( int fd, 
			uint16_t fw_ver,
			uint16_t addr,
			uint8_t *chunk );

/* Send the given section to the msp430.
   Arguments:
    - 	       fd: The I2C file descriptor
    -     section: The section to send
    - check_first: FALSE means ignore the first expected address read from the MSP430.
    		   This is useful for when the msp430 will accept data for
		   another block of memory -- i.e. the IVT. */
void msp430_send_section( int fd,
			  elf_section_t *section, 
			  gboolean check_first );

/* Confirm that the checksum the msp430 calculated is valid */
void msp430_confirm_crc( int i2c_fd );

#endif	/* __MSP430_FW */
