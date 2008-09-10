#ifndef __SMBUS_PEC_H
#define __SMBUS_PEC_H
#include <stdint.h>

#define USE_CHECKSUMS 1

uint8_t crc8( uint8_t tempdata );

#endif	/* __SMBUS_PEC_H */
