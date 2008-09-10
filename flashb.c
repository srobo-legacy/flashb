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

/* A utility for loading firmware into a MSP430 over I2C */
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "i2c-dev.h"
#include "elf-access.h"
#include "i2c.h"
#include "smbus_pec.h"

#define CHUNK_SIZE 16

static const char* config_fname = "flashb.config";

static char* i2c_device = NULL;
static uint8_t i2c_address = 0;

/* Read configuration from a file */
static void config_load( const char* fname );

/* Read a single byte value from a GKeyFile that's in hex.
 * Returns the value. */
static uint8_t key_file_get_hex( GKeyFile *key_file,
				 const gchar *group_name,
				 const gchar *key,
				 GError **error );

/** Firmware related I2C commands **/
/* Read firmware from the msp430 */
#define	CMD_FW_VER 4
/* Send a chunk to the msp430 */
#define	CMD_FW_CHUNK 5
/* Read the next address that the msp430 expects */
#define CMD_FW_NEXT 5
/* Read the CRC of the firmware calculated on the msp430 */
#define	CMD_FW_CRCR 6
/* Confirm the firmware CRC, triggering switchover */
#define CMD_FW_CONFIRM 6

/* Read the firmware version from the device */
static uint16_t msp430_get_fw_version( int fd );

/* Read the next address the device is expecting */
static uint16_t msp430_get_next_address( int fd );

static uint16_t msp430_get_next_address_once( int fd );

/* Send a 16 byte chunk of firmware to the msp430.
   Arguments:
    -     fd: The i2c device file descriptor
    - fw_ver: The firmware version
    -   addr: The chunk address
    -  chunk: Pointer to the 16 byte chunk of data */
static void msp430_send_block( int fd, 
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
static void msp430_send_section( int fd,
				 elf_section_t *section, 
				 gboolean check_first );

/* Confirm that the checksum the msp430 calculated is valid */
static void msp430_confirm_crc( int i2c_fd );

int main( int argc, char** argv )
{
	elf_section_t *text = NULL, *vectors = NULL;
	int i2c_fd;
	uint16_t fw;

	/** Load config and setup **/
	config_load( config_fname );
	i2c_fd = i2c_config( i2c_device, i2c_address );

	elf_access_load_sections( "motor", &text, &vectors );
	if( vectors->len != 32 )
		g_error( ".vectors section incorrect length: %u should be 32", vectors->len );

	printf( ".text: len=%u, addr=0x%x\n", text->len, text->addr );
	printf( ".vectors: len=%u, addr=0x%x\n", vectors->len, vectors->addr );

	/* Get the firmware version.
	   The MSP430 resets its firmware reception code upon receiving this. */
	fw = msp430_get_fw_version( i2c_fd );
	printf( "Existing firmware version %hx\n", fw );

	printf( "Writing .text\n" );
	msp430_send_section( i2c_fd, text, TRUE );
	printf( "Writing .vectors\n" );
	msp430_send_section( i2c_fd, vectors, FALSE );
	printf( "Confirming CRC\n" );
	msp430_confirm_crc( i2c_fd );

	return 0;
}

static void config_load( const char* fname )
{
	GError *err = NULL;
	GKeyFile *keyfile;

	keyfile = g_key_file_new();
	g_key_file_load_from_file( keyfile,
				   fname, 
				   0, &err );
	if( err != NULL )
		g_error( "Failed to load config from file '%s': %s", 
			 fname, err->message );

	if( !g_key_file_has_group( keyfile, "i2c" ) )
		g_error( "i2c group not found in config file" );

	if( !g_key_file_has_key( keyfile, "i2c", "device", NULL ) )
		g_error( "i2c.device config not found" );

	err = NULL;
	i2c_device = g_key_file_get_string( keyfile, "i2c", "device", &err );
	if( err != NULL )
		g_error( "Failed to read i2c.device: %s", err->message );

	if( !g_key_file_has_group( keyfile, "motor" ) )
		g_error( "motor group not found in config file" );
	if( !g_key_file_has_key( keyfile, "motor", "address", NULL ) )
		g_error( "motor.address config not found" );
	
	err = NULL;
	i2c_address = key_file_get_hex( keyfile, "motor", "address", &err );
	if( err != NULL )
		g_error( "Failed to read motor.address: %s", err->message );
	printf( "device address: 0x%hhx\n", i2c_address );
}

static uint8_t key_file_get_hex( GKeyFile *key_file,
				 const gchar *group_name,
				 const gchar *key,
				 GError **error )
{
	gchar *tmp;
	unsigned long int v;

	g_assert( key_file != NULL 
		  && group_name != NULL 
		  && key != NULL );

	if( error != NULL )
		*error = NULL;

	tmp = g_key_file_get_string( key_file,
				     group_name, 
				     key,
				     error );
	
	if( error != NULL && *error != NULL ) 
		return 0;

	errno = 0;
	v = strtoul( tmp, NULL, 16 );
	if( errno != 0 )
		g_error( "Failed to read hex value %s.%s (Should do some GError stuff here", group_name, key );

	g_free( tmp );

	return v;
}

static uint16_t msp430_get_fw_version( int fd )
{
	int32_t r;

	r = i2c_smbus_read_word_data( fd, CMD_FW_VER );
	if( r < 0 )
		g_error( "Failed to read firmware version: %m" );

	g_assert( r <= 0xffff );

	return r;
}

static uint16_t msp430_get_next_address( int fd )
{
	int32_t r1, r2;

	do {
	r1 = msp430_get_next_address_once(fd);
	r2 = msp430_get_next_address_once(fd);
	} while ( r1 != r2 );

	return r1;
}

static void msp430_send_block( int fd, 
			       uint16_t fw_ver,
			       uint16_t addr,
			       uint8_t *chunk )
{
	uint8_t b[ 6 + CHUNK_SIZE];
	uint8_t c, i;
	int w;
	/* We have to do the block transmit ourselves */

	/* Format:
	    0: Command
	    1-2: Firmware version (1 is lsb)
	    3-4: Address (3 is lsb)
	   5-20: The data
	     21: The checksum */

	b[0] = CMD_FW_CHUNK;
	b[1] = fw_ver & 0xff;
	b[2] = (fw_ver >> 8) & 0xff;
	b[3] = addr & 0xff;
	b[4] = (addr >> 8) & 0xff;

	g_memmove( b + 5, chunk, CHUNK_SIZE );

	/* Checksum the lot */
	c = crc8( i2c_address << 1 );
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

static uint16_t msp430_get_next_address_once( int fd )
{
	int32_t r;

	do {
	r = i2c_smbus_read_word_data( fd, CMD_FW_NEXT );
	if( r < 0 )
		printf( "Failed to read next address: %m\n" );
	} while ( r < 0 );

	g_assert( r <= 0xffff );
	printf( "next: %hx\n", (uint16_t)r );

	return r;
}

static void msp430_send_section( int i2c_fd,
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

static void msp430_confirm_crc( int i2c_fd )
{
	uint8_t buf[6], i, c;

	/* Format:
	 * 0: Command (CMD_FW_CONFIRM)
	 * 1-4: Password (currently ignored)
	 * 5: CRC */
	buf[0] = CMD_FW_CONFIRM;
	buf[1] = buf[2] = buf[3] = buf[4] = 0;

	/* TODO move block transmission into function */
	c = crc8( i2c_address << 1 );
	for( i=0; i < 5; i++)
		c = crc8( c ^ buf[i] );
	buf[5] = c;

	i2c_pec_disable(i2c_fd);

	if( write( i2c_fd, buf, 6 ) != 6 )
		g_error( "Failed to send confirmation command" );

	i2c_pec_enable(i2c_fd);
}
