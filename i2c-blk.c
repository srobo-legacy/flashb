#include "i2c-blk.h"
#include "i2c.h"
#include "i2c-dev.h"
#include "smbus_pec.h"
#include <stdio.h>
#include <unistd.h>
#include <glib.h>
#include <string.h>

static uint8_t blk_buf[I2C_BLK_LEN + 3];
uint8_t *i2c_blk_buf = blk_buf + 2;

uint8_t *i2c_blk_addr;

/* We have to hack around the fact that the i2c adapter doesn't
 * support i2c block read. 
 * First, do a read to get the length byte.  This read byte operation
 * also sets the command that we're doing within the powerboard */
int32_t i2c_blk_read( int fd, uint8_t reg )
{
	int len, r;
	uint8_t checksum, i;

	i2c_pec_disable(fd);
	
	/* Set the command and grab the length */
	len = i2c_smbus_read_byte_data( fd, reg );

	if( len < 0 ) {
		fprintf( stderr, "Failed to read register %hhu length\n", reg );
		goto error0;
	}

	/* Prevent buffer overflows */
	if( len+3 > I2C_BLK_LEN ) {
		fprintf( stderr, "Register too long\n" );
		goto error0;
	}

	r = read(fd, blk_buf, len + 3 );
	
	if( r < 0 ) {
		fprintf( stderr, "Failed to read register %hhu\n", reg );
		goto error0;
	}

	if( r != len + 3 ) {
		fprintf( stderr, "Failed to read all of register %hhu\n", reg );
		goto error0;
	}

	/* Generate the checksum: */
	checksum = crc8( (*i2c_blk_addr<<1) | 1 );
	for( i=0; i<len+2; i++ )
		checksum = crc8( checksum ^ (blk_buf)[i] );

	if( (blk_buf)[r-1] != checksum ) {
		if( 1 )
			fprintf( stderr, "Incorrect checksum reading register %hhu\n", reg );
		printf( "Checksums: received = 0x%2.2hhx, calculated = 0x%2.2hhx\n",
			(blk_buf)[len+2],
			checksum );
		/* Checksum's incorrect */
		goto error0;
	}

	if( (blk_buf)[1] != reg ) {
		fprintf( stderr, "Incorrect register read %hhu\n", reg );

		/* Incorrect command read back */
		goto error0;
	}

	if( 0 )	{
		uint8_t i;
		printf( "\tRead %i bytes from register %hhu:\n", len, reg );
		for( i=0; i<len+3; i++ )
			printf( "\t\t%hhX: 0x%2.2hhX\n", i, (blk_buf)[i] );
	}

	i2c_pec_enable(fd);
	return len;

error0:
	i2c_pec_enable(fd);
	return -1;
}

int32_t i2c_blk_write( int fd, uint8_t command, uint8_t len, uint8_t *buf )
{
	uint8_t lbuf[len + 3];
	int w;
	uint8_t checksum, i;
	g_assert( buf != NULL && len < 33 );

	/* Duplicate the buffer so we can prepend the register number
	   and byte count */
	memcpy( lbuf+2, buf, len );
	lbuf[0] = command;
	lbuf[1] = len;

	/* Calculate the PEC */
	checksum = crc8( *i2c_blk_addr<<1 );
	for( i=0; i<len+2; i++ )
		checksum = crc8( checksum ^ lbuf[i] );

	lbuf[len+2] = checksum;

	i2c_pec_disable(fd);
	w = write( fd, lbuf, len+3 );
	i2c_pec_enable(fd);

	if( w < 0 ) {
		fprintf( stderr, "Error writing command %hhu\n", command );
		return -1;
	} else if( w < len + 3 ) {
		fprintf( stderr, "Could not write enough data to command %hhu\n", command );
		return -1;
	}

	return 0;
}
