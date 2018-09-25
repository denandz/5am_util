/*
*   File: fw.h
*   Author: DoI
*/

#ifndef FW_H
#define FW_H

#include <stdint.h>

struct fw {
    uint64_t bin_len;
    uint8_t * bin;
    uint64_t enc_len;
    uint8_t * enc;
    uint16_t checksum;
};

#endif
