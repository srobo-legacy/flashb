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
#ifndef __I2C_H
#define __I2C_H
#include <stdint.h>

/* Open the i2c device and configure it.
   Returns the file descriptor. */
int i2c_config( char* devname, uint8_t address );

/* Enable packet error checking */
void i2c_pec_enable( int fd );
/* Disable packet error checking */
void i2c_pec_disable( int fd );

/* Send a block of data to the client.
   Include the checksum in the packet.
   Outgoing packet format:
   Address, Command, Data, PEC
   Please note the absence of the length field. */
int i2c_send_block( int fd,
		    uint8_t cmd,
		    uint8_t *data,
		    uint8_t len );

#endif	/* __I2C_H */
