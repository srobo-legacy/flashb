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
#include "i2c.h"
#include <glib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "i2c-dev.h"
#include <stdio.h>

int i2c_config( char* devname, uint8_t address )
{
	int fd;
	g_assert( devname != NULL );
	g_assert( address != 0 );

	fd = open( devname, O_RDWR );

	if( fd == -1 )
		g_error( "Failed to open i2c device '%s': %m", devname );

	/* Set the slave address */
	if( ioctl( fd, I2C_SLAVE, address ) < 0 )
		g_error( "Failed to set slave address to 0x%hhx: %m\n", address );

	/* TODO: Config option? */
	i2c_pec_enable( fd );

	return fd;
}

void i2c_pec_enable( int fd )
{
	if( ioctl( fd, I2C_PEC, 1) < 0) 
		g_error( "Failed to enable PEC\n" ); 
}

void i2c_pec_disable( int fd )
{
	if( ioctl( fd, I2C_PEC, 0) < 0) 
		g_error( "Failed to disable PEC\n" ); 
}

int i2c_send_block( int fd,
		    uint8_t cmd,
		    uint8_t *data,
		    uint8_t len )
{
	return -1;	
}
