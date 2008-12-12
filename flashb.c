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
#include "msp430-fw.h"

/* Sort out all the configuration loading from the cli and config file */
static void config_load( int *argc, char ***argv );

/* Read configuration from a file */
static void config_file_load( const char* fname );

/* Read a single byte value from a GKeyFile that's in hex.
 * Returns the value. */
static uint8_t key_file_get_hex( GKeyFile *key_file,
				 const gchar *group_name,
				 const gchar *key,
				 GError **error );

/** Firmware related I2C commands **/
typedef struct {
	uint8_t cmd;
	char* conf_name;
} cmd_desc_t;

cmd_desc_t cmds[] =
{
	{ CMD_FW_VER, "cmd_fw_ver" },
	{ CMD_FW_CHUNK, "cmd_fw_chunk" },
	{ CMD_FW_NEXT, "cmd_fw_next" },
	{ CMD_FW_CRCR, "cmd_fw_crcr" },
	{ CMD_FW_CONFIRM, "cmd_fw_confirm" }
};

/* Returns the string for the given command number.
   Looks up the command number in the cmds table. */
char* conf_get_cmd_str( uint8_t cmd );

static char* config_fname = "flashb.config";
static char* i2c_device = NULL;
static uint8_t i2c_address = 0;
static char* dev_name = NULL;
static char* elf_fname = NULL;

static GOptionEntry entries[] =
{
	{ "config", 'c', 0, G_OPTION_ARG_FILENAME, &config_fname, "Config file path", "PATH" },
	{ "device", 'd', 0, G_OPTION_ARG_FILENAME, &i2c_device, "I2C device path", "DEV_PATH" },
	{ "name", 'n', 0, G_OPTION_ARG_STRING, &dev_name, "Slave device name in config file.", "NAME" },
	{ NULL }
};

int main( int argc, char** argv )
{
	elf_section_t *text = NULL, *vectors = NULL;
	int i2c_fd;
	uint16_t fw;

	config_load( &argc, &argv );
	i2c_fd = i2c_config( i2c_device, i2c_address );

	/* Tell the msp430_fw code what the address is  */
	msp430_fw_i2c_address = &i2c_address;

	elf_access_load_sections( elf_fname, &text, &vectors );
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

static void config_file_load( const char* fname )
{
	GError *err = NULL;
	GKeyFile *keyfile;
	uint8_t i;

	keyfile = g_key_file_new();
	g_key_file_load_from_file( keyfile,
				   fname, 
				   0, &err );
	if( err != NULL )
		g_error( "Failed to load config from file '%s': %s", 
			 fname, err->message );

	/** Load the I2C device name **/
	/* Check the i2c group exists */
	if( !g_key_file_has_group( keyfile, "i2c" ) )
		g_error( "i2c group not found in config file" );

	/* Check the key exists */
	if( !g_key_file_has_key( keyfile, "i2c", "device", NULL ) )
		g_error( "i2c.device config not found" );

	err = NULL;
	i2c_device = g_key_file_get_string( keyfile, "i2c", "device", &err );
	if( err != NULL )
		g_error( "Failed to read i2c.device: %s", err->message );

	/** Load the I2C slave address **/
	/* Check the device group exists */
	if( !g_key_file_has_group( keyfile, dev_name ) )
		g_error( "%s group not found in config file", dev_name );

	/* Check it's in the config file */
	if( !g_key_file_has_key( keyfile, dev_name, "address", NULL ) )
		g_error( "%s.address config not found", dev_name );
	
	/* Grab it from the config file */
	err = NULL;
	i2c_address = key_file_get_hex( keyfile, dev_name, "address", &err );
	if( err != NULL )
		g_error( "Failed to read %s.address: %s", dev_name, err->message );
	printf( "device address: 0x%hhx\n", i2c_address );

	/** Load in the commands **/
	/* dev_name */
	for( i=0; i<NUM_COMMANDS; i++ ) {
		err = NULL;
		if( !g_key_file_has_key( keyfile, dev_name, conf_get_cmd_str(i), &err ) )
			g_error( "%s board has no %s command defined.",
				 dev_name, conf_get_cmd_str(i) );

		err = NULL;
		commands[i] = g_key_file_get_integer( keyfile, dev_name,
						      conf_get_cmd_str(i), &err );

		g_print( "Command %s is %hhu\n", conf_get_cmd_str(i), commands[i] );
	}
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

static void config_load( int *argc, char ***argv )
{
	GError *error = NULL;
	GOptionContext *context;

	context = g_option_context_new( "ELF_FILE - flash MSP430s over I2C" );
	g_option_context_add_main_entries( context, entries, NULL );

	/* Parse command line options */
	if( !g_option_context_parse( context, argc, argv, &error ) ) {
		g_print( "Failed to parse command line options: %s\n",
			 error->message );
		exit(1);
	}

	/* Device must be specified */
	if( dev_name == NULL ) {
		g_print( "Error: No device name specified.  See --help\n");
		exit(1);
	}

	/* Argument without letter is the elf filename */
	if( *argc != 2 ) {
		g_print ( "Error: ELF file name required.  See --help\n" );
		exit(1);
	}
	elf_fname = (*argv)[1];

	/* Load settings from the config file  */
	config_file_load( config_fname );
}

char* conf_get_cmd_str( uint8_t cmd )
{
	uint8_t i;
	g_assert( (sizeof(cmds)/sizeof(cmd_desc_t)) == NUM_COMMANDS );

	for( i=0; i<NUM_COMMANDS; i++ ) {
		if( cmds[i].cmd == cmd )
			return cmds[i].conf_name;
	}

	g_error( "Command number %hhu not found in table", cmd );
	return NULL;
}
