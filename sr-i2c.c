#include "sr-i2c.h"
#include "i2c-blk.h"
#include "i2c-dev.h"
#include "smbus_pec.h"
#include "i2c.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>
#include <string.h>

sr_i2c_cmd_t sr_i2c_cmd_format = SR_I2C_NO_LEN;

int sr_i2c_read_byte( int fd, uint8_t cmd, uint8_t *b )
{
	assert( b != NULL );

	switch( sr_i2c_cmd_format ) {
	case SR_I2C_NO_LEN: {
		int r;
		r = i2c_smbus_read_byte_data( fd, cmd );

		if( r < 0 )
			return -1;
		*b = r;
	} break;

	case SR_I2C_MAGIC: {
		int r = i2c_blk_read( fd, cmd );

		if( r < 0 )
			return -1;
		else if( r != 1 )
			return -1;
		else
			*b = i2c_blk_buf[0];
	} break;
	}

	return 0;
}

int sr_i2c_read_word( int fd, uint8_t cmd, uint16_t *b )
{
	assert( b != NULL );

	switch( sr_i2c_cmd_format ) {
	case SR_I2C_NO_LEN: {
		int r;
		r = i2c_smbus_read_word_data( fd, cmd );

		if( r < 0 )
			return -1;
		*b = r;
	} break;

	case SR_I2C_MAGIC: {
		int r = i2c_blk_read( fd, cmd );

		if( r < 0 )
			return -1;
		else if( r != 2 )
			return -1;
		else
			*b = i2c_blk_buf[0] | (((uint16_t)i2c_blk_buf[1]) << 8);
	} break;
	}

	return 0;
}

int sr_i2c_write_byte( int fd, uint8_t cmd, uint8_t b )
{
	switch( sr_i2c_cmd_format ) {
	case SR_I2C_NO_LEN: {
		int r;
		r = i2c_smbus_write_byte_data( fd, cmd, b );

		if( r < 0 )
			return -1;
	} break;

	case SR_I2C_MAGIC: {
		int r = i2c_blk_write( fd, cmd, 1, &b );

		if( r < 0 )
			return -1;
	} break;
	}

	return 0;
}

int sr_i2c_write_word( int fd, uint8_t cmd, uint16_t b )
{
	switch( sr_i2c_cmd_format ) {
	case SR_I2C_NO_LEN: {
		int r;
		r = i2c_smbus_write_word_data( fd, cmd, b );

		if( r < 0 )
			return -1;
	} break;

	case SR_I2C_MAGIC: {
		uint8_t buf[2];
		int r;

		buf[0] = b & 0xff;
		buf[1] = (b >> 8) & 0xff;

		r = i2c_blk_write( fd, cmd, 2, buf );

		if( r < 0 )
			return -1;
	} break;
	}

	return 0;
}

int sr_i2c_block_read( int fd, uint8_t cmd, uint8_t *len, uint8_t **buf )
{
	fprintf( stderr, "ERROR: SMBus block read not implemented\n" );
	exit(-1);

	return -1;
}

int sr_i2c_block_write( int fd, uint8_t cmd, uint8_t len, uint8_t *buf, uint8_t address )
{
	assert( buf != NULL );

	switch( sr_i2c_cmd_format ) {
	case SR_I2C_NO_LEN: {
		/* Include the command on the front and the checksum on the end */
		uint8_t lb[len + 2];
		int w;
		uint8_t c, i;

		lb[0] = cmd;
		g_memmove( lb+1, buf, len );
		
		c = crc8( address << 1 );
		for( i=0; i<len+1; i++ )
			c = crc8( c ^ lb[i] );
		lb[len+1] = c;

		i2c_pec_disable(fd);
		w = write( fd, lb, len+2 );

		if( w == -1 )
			g_error( "Failed to send block: %m\n" );
		if( w != len +2 ) 
			g_error( "Failed to send all data.  Only sent %i of %hhu\n",
				 w, len+2 );
		i2c_pec_enable(fd);

	} break;

	case SR_I2C_MAGIC: {
		int r;

		r = i2c_blk_write( fd, cmd, len, buf );
		if( r < 0 )
			return -1;
	} break;
	}

	return 0;
}
