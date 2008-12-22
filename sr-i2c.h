/* Routines that abstract I2C byte, word and block read/write functions
 * into the magical block read/write format if necessary */
#ifndef __SR_I2C_H
#define __SR_I2C_H
#include <stdint.h>

/* Types of command formats */
typedef enum {
	/* Block writes are fixed length and have no length byte.
	   Bytes and word read/writes are SMBus byte and word read/writes. */
	SR_I2C_NO_LEN = 1,

	/* All reads and writes are SR magical block reads and writes */
	SR_I2C_MAGIC
} sr_i2c_cmd_t;

extern sr_i2c_cmd_t sr_i2c_cmd_format;

int sr_i2c_read_byte( int fd, uint8_t cmd, uint8_t *r );

int sr_i2c_read_word( int fd, uint8_t cmd, uint16_t *r );

int sr_i2c_write_byte( int fd, uint8_t cmd, uint8_t b );

int sr_i2c_write_word( int fd, uint8_t cmd, uint16_t b );

int sr_i2c_block_read( int fd, uint8_t cmd, uint8_t *len, uint8_t **buf );

int sr_i2c_block_write( int fd, uint8_t cmd, uint8_t len, uint8_t *buf, uint8_t address );

#endif	/* __SR_I2C_H */
