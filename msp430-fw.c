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
#include "msp430-fw.h"

uint8_t commands[NUM_COMMANDS];
uint8_t* msp430_fw_i2c_address = NULL;
uint16_t msp430_fw_bottom = 0;
uint16_t msp430_fw_top = 0;

uint16_t msp430_get_fw_version( int fd )
{
	int32_t r;

	r = i2c_smbus_read_word_data( fd, commands[CMD_FW_VER] );
	if( r < 0 )
		g_error( "Failed to read firmware version: %m" );

	g_assert( r <= 0xffff );

	return r;
}

uint16_t msp430_get_next_address( int fd )
{
	int32_t r1, r2;

	do {
		r1 = msp430_get_next_address_once(fd);
		r2 = msp430_get_next_address_once(fd);
	} while ( r1 != r2 );

	return r1;
}

void msp430_send_block( int fd, 
			uint16_t fw_ver,
			uint16_t addr,
			uint8_t *chunk )
{
	uint8_t b[6 + CHUNK_SIZE];
	uint8_t c, i;
	int w;
	/* We have to do the block transmit ourselves */

	/* Format:
	   0: Command
	   1-2: Firmware version (1 is lsb)
	   3-4: Address (3 is lsb)
	   5-20: The data
	   21: The checksum */

	b[0] = commands[CMD_FW_CHUNK];
	b[1] = fw_ver & 0xff;
	b[2] = (fw_ver >> 8) & 0xff;
	b[3] = addr & 0xff;
	b[4] = (addr >> 8) & 0xff;

	g_memmove( b + 5, chunk, CHUNK_SIZE );

	/* Checksum the lot */
	c = crc8( (*msp430_fw_i2c_address) << 1 );
	for( i=0; i < (5+CHUNK_SIZE); i++)
		c = crc8( c ^ b[i] );

	b[5 + CHUNK_SIZE] = c;

	printf( "Sending chunk with address %hx\n", addr );

/* 	for( i=0; i<(6+CHUNK_SIZE); i++ ) */
/* 		printf( "%hhu: %hhx\n", i, b[i] ); */

	/* Disable the PEC for this operation */
	i2c_pec_disable(fd);

	w = write( fd, b, 6 + CHUNK_SIZE );
	if( w == -1 )
		g_error( "Failed to send block: %m\n" );
	if( w != 6 + CHUNK_SIZE )
		g_error( "Failed to write all data.  Only sent %i of %i",
			 w, 6 + CHUNK_SIZE );

	i2c_pec_enable(fd);
}

uint16_t msp430_get_next_address_once( int fd )
{
	int32_t r;

	do {
		r = i2c_smbus_read_word_data( fd, commands[CMD_FW_NEXT] );
		if( r < 0 )
			printf( "Failed to read next address: %m\n" );
	} while ( r < 0 );

	g_assert( r <= 0xffff );
	printf( "next: %hx\n", (uint16_t)r );

	return r;
}

void msp430_send_section( int i2c_fd,
			  elf_section_t *section, 
			  gboolean check_first )
{
	uint16_t next;
	g_assert( section != NULL );

	if( check_first ) {
		next = msp430_get_next_address( i2c_fd );
		printf( "msp430 waiting for address %hx\n", next );

		if( next != section->addr )
			g_error( "I've got the wrong binary -- need one that starts at %hx, got %hx\n", next, section->addr );
	}
	else
		next = section->addr;

	while( next < (section->addr + section->len) 
	       /* MSP430 indicates all firmware received with 0 */
	       && next != 0 ) {
		uint16_t rem;
		uint8_t *chunk;

		/* Must be CHUNK_SIZE aligned */
		g_assert( next % CHUNK_SIZE == 0 );
		g_assert( next >= section->addr );

		chunk = section->data + (next - section->addr);
		rem = section->len - (next - section->addr);
		printf( "%hu bytes remaining\n", rem );

		if( rem < CHUNK_SIZE ) {
			/* Pad out to 16 bytes long */
			uint8_t b[CHUNK_SIZE];
			uint8_t i;

			g_memmove( b, chunk, rem );
			for( i=rem; i<CHUNK_SIZE; i++ )
				b[i] = 0xaa;

			msp430_send_block( i2c_fd, 
					   0, 
					   next, 
					   chunk );
		}
		else
			msp430_send_block( i2c_fd, 
					   0, 
					   next, 
					   chunk );

		next = msp430_get_next_address( i2c_fd );

		/* May have failed */
		if( check_first && next < section->addr )
			next = section->addr;
	}
}

void msp430_confirm_crc( int i2c_fd )
{
	uint8_t buf[6], i, c;

	/* Format:
	 * 0: Command (CMD_FW_CONFIRM)
	 * 1-4: Password (currently ignored)
	 * 5: CRC */
	buf[0] = commands[CMD_FW_CONFIRM];
	buf[1] = buf[2] = buf[3] = buf[4] = 0;

	/* TODO move block transmission into function */
	c = crc8( (*msp430_fw_i2c_address) << 1 );
	for( i=0; i < 5; i++)
		c = crc8( c ^ buf[i] );
	buf[5] = c;

	i2c_pec_disable(i2c_fd);

	if( write( i2c_fd, buf, 6 ) != 6 )
		g_error( "Failed to send confirmation command" );

	i2c_pec_enable(i2c_fd);
}
