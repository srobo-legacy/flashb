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
#include <sric.h>

#include "elf-access.h"
#include "msp430-fw.h"

/* Sort out all the configuration loading from the cli and config file */
static void config_load( int *argc, char ***argv );

/* Read configuration from a file */
static void config_file_load( const char* fname );

/* Read a single byte value from a GKeyFile that's in hex.
 * Returns the value. */
static unsigned long int key_file_get_hex( GKeyFile *key_file,
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
static uint8_t board_type = 0;
static char* dev_name = NULL;
static char *elf_fname_b = NULL;
static char *elf_fname_t = NULL;
static gboolean force_load = FALSE;
static gint board_address = 0;

static GOptionEntry entries[] =
{
	{ "config", 'c', 0, G_OPTION_ARG_FILENAME, &config_fname, "Config file path", "PATH" },
	{ "name", 'n', 0, G_OPTION_ARG_STRING, &dev_name, "Slave device name in config file.", "NAME" },
	{ "force", 'f', 0, G_OPTION_ARG_NONE, &force_load, "Force update, even if target has given version", NULL },
	{ "address", 'a', 0, G_OPTION_ARG_INT, &board_address, "Only program board at address n", "n" },
	{ NULL }
};

struct elf_file_t {
	elf_section_t *text, *vectors;
};

/* Open the two ELF files and work out which is top and which is bottom.
 * Returns the two files in *bottom and *top. */
static void load_elfs( char* fna, char* fnb,
		       struct elf_file_t *bottom,
		       struct elf_file_t *top );

/* Get the version number of the given ELF file */
static uint16_t elf_fw_version( struct elf_file_t *e );

/* Program the given device after making a few sanity checks.
 * Returns TRUE when flashing succeeded */
static gboolean flash_board( const sric_context ctx,
                             const sric_device *device,
                             struct elf_file_t *elf,
                             const uint16_t fw );

int main( int argc, char** argv )
{
	sric_context ctx;
	uint16_t fw, next;
	struct elf_file_t ef_top, ef_bottom;
	struct elf_file_t *tos;

	config_load( &argc, &argv );

	ctx = sric_init();
	if (sric_get_error(ctx) & SRIC_ERROR_SRICD) {
		g_print("Failed to connect to sricd.\n");
		return 0;
	}

	/* Load and sort the ELF files */
	load_elfs( elf_fname_b, elf_fname_t, &ef_bottom, &ef_top );

	const sric_device* device = NULL;
	/* Used to keep count of the index of the current board type */
	while((device = sric_enumerate_devices(ctx, device))) {
		g_print("Address: %i\tType: %i\n", device->address, device->type);

		if (board_address != 0) {
			if (board_address != device->address)
				continue;
			else if (board_type != device->type)
				g_error("Board at address %i is not the correct type", board_address);
		} else if (device->type != board_type)
			continue;

		/* Get the firmware version.
		   The MSP430 resets its firmware reception code upon receiving this. */
		if( !msp430_get_fw_version( ctx, device, &fw ) ) {
			g_print( "'%s[%i]' not answering\n", dev_name, device->address );
			return FALSE;
		}

		/* Find out which ELF file to send (top or bottom) */
		next = msp430_get_next_address( ctx, device );
		if( next == msp430_fw_bottom ) {
			g_print("Sending bottom half\n");
			tos = &ef_bottom;
		}
		else if( next == msp430_fw_top ) {
			g_print("Sending top half\n");
			tos = &ef_top;
		}
		else
			g_error( "MSP430 is requesting unexpected address: 0x%4.4hx", next );

		if( elf_fw_version( &ef_bottom ) != elf_fw_version( &ef_top ) )
			g_error( "Supplied ELF files have different version numbers" );

		flash_board(ctx, device, tos, fw);
	}

	sric_quit(ctx);

	return 0;
}

static gboolean flash_board( const sric_context ctx,
                             const sric_device *device,
                             struct elf_file_t *elf,
                             const uint16_t fw) {


		printf( "Existing firmware version on '%s[%i]': %hx\n", dev_name, device->address, fw );

		if( elf->vectors->len != 32 ) {
			g_print( ".vectors section incorrect length: %u should be 32", elf->vectors->len );
			return FALSE;
		}

		if( !force_load && fw == elf_fw_version( elf ) ) {
			g_print( "No update required\n" );
			return FALSE;
		}

		printf("Sending firmware version %hu to '%s[%i]'\n", elf_fw_version(elf), dev_name, device->address);

		msp430_send_section( ctx, device, elf->text, TRUE );
		msp430_send_section( ctx, device, elf->vectors, FALSE );
		printf( "Confirming CRC\n" );
		msp430_confirm_crc( ctx, device );

		return TRUE;
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

	/* Check for the board type exists */
	if (!g_key_file_has_key(keyfile, dev_name, "board", NULL))
		g_error("%s.board config not found", dev_name);

	err = NULL;
	board_type = key_file_get_hex(keyfile, dev_name, "board", &err);
	if (err != NULL)
		g_error("Failed to read %s.board: %s", dev_name, err->message);

	/** Load in the commands **/
	/* dev_name */
	for( i=0; i<NUM_COMMANDS; i++ ) {
		char *key = conf_get_cmd_str(i);

		err = NULL;
		if( !g_key_file_has_key( keyfile, dev_name, key, &err ) )
			g_error( "%s board has no %s command defined.",
				 dev_name, key );

		err = NULL;
		commands[i] = g_key_file_get_integer( keyfile, dev_name,
						      key, &err );
		if( err != NULL )
			g_error( "Failed to read %s.%s from config file: %s", dev_name, key, err->message );
	}

	/* Grab the top and bottom addresses */
	if( !g_key_file_has_key( keyfile, dev_name, "bottom", NULL ) )
		g_error( "%s.bottom config not found", dev_name );
	if( !g_key_file_has_key( keyfile, dev_name, "top", NULL ) )
		g_error( "%s.top config not found", dev_name );

	msp430_fw_bottom = key_file_get_hex( keyfile, dev_name, "bottom", NULL );
	msp430_fw_top = key_file_get_hex( keyfile, dev_name, "top", NULL );
}

static unsigned long int key_file_get_hex( GKeyFile *key_file,
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

	context = g_option_context_new( "BOTTOM_ELF_FILE TOP_ELF_FILE - flash MSP430s over I2C" );
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
	if( *argc != 3 ) {
		g_print( "Error: Two ELF files required.  See --help\n" );
		exit(1);
	}

	elf_fname_b = (*argv)[1];
	elf_fname_t = (*argv)[2];

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

static void load_elfs( char* fna, char* fnb,
		       struct elf_file_t *bottom,
		       struct elf_file_t *top )
{
	g_assert( bottom != NULL && top != NULL );

	/* Load the ELFs */
	elf_access_load_sections( fna, &bottom->text, &bottom->vectors );
	elf_access_load_sections( fnb, &top->text, &top->vectors );

	if( bottom->text->addr > top->text->addr ) {
		/* Swap them */
		struct elf_file_t tmp = *bottom;

		*bottom = *top;
		*top = tmp;
	}

	if( bottom->text->addr != msp430_fw_bottom )
		g_error( "Lower ELF file has .text offset %hx -- should be %hx\n", bottom->text->addr, msp430_fw_bottom );

	if( top->text->addr != msp430_fw_top )
		g_error( "Upper ELF file has .text offset %hx -- should be %hx\n", top->text->addr, msp430_fw_top );

}

static uint16_t elf_fw_version( struct elf_file_t *e )
{
	uint16_t ver = 0;

	if( e->text->len < 2 )
		g_error( ".text section is too short to contain firmware version!" );

	ver = e->text->data[0];
	ver |= ((uint16_t)e->text->data[1]) << 8;

	return ver;
}
