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
#include "i2c-dev.h"
#include "elf-access.h"
#include "i2c.h"

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

int main( int argc, char** argv )
{
	elf_section_t *text = NULL, *vectors = NULL;
	int i2c_fd;
	uint16_t fw, next;

	/** Load config and setup **/
	config_load( config_fname );
	i2c_fd = i2c_config( i2c_device, i2c_address );

	elf_access_load_sections( "motor", &text, &vectors );
	if( vectors->len != 32 )
		g_error( ".vectors section incorrect length: %u should be 32", vectors->len );

	printf( ".text: len=%u, addr=0x%x\n", text->len, text->addr );
	printf( ".vectors: len=%u, addr=0x%x\n", vectors->len, vectors->addr );

	fw = msp430_get_fw_version( i2c_fd );
	printf( "Existing firmware version %hx\n", fw );
	
	next = msp430_get_next_address( i2c_fd );
	printf( "msp430 waiting for address %hx\n", next );

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
	int32_t r;

	r = i2c_smbus_read_word_data( fd, CMD_FW_NEXT );
	if( r < 0 )
		g_error( "Failed to read next address: %m" );

	g_assert( r <= 0xffff );

	return r;
}
