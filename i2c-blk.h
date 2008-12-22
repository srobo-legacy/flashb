/* Commands for reading and writing in block form */
#ifndef __I2C_BLK_H
#define __I2C_BLK_H
#include <stdint.h>

/* Read data from register number reg from the slave.
 * Returns the number of bytes read.  Reads the data into i2c_blk_buf.
 * Returns -1 on failure. */
int32_t i2c_blk_read( int fd, uint8_t reg );

/* Write len bytes to the slave from the given buffer. */
int32_t i2c_blk_write( int fd, uint8_t command, uint8_t len, uint8_t *buf );

#define I2C_BLK_LEN 34
/* Pointer to read buffer length I2C_BLK_LEN */
extern uint8_t *i2c_blk_buf;

/* The slave address */
extern uint8_t *i2c_blk_addr;

#endif	/* __I2C_BLK_H */

