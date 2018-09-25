/*
*   File: util.h
*   Author: DoI
*/

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

#define fatal(x...) \
    do { \
        fprintf(stderr, RED "[!] ERROR: " RESET); \
        fprintf(stderr, x); \
        fprintf(stderr, "\n         Location : %s(), %s:%d\n\n", \
        __FUNCTION__, __FILE__, __LINE__); \
        exit(0);\
    } while(0)

#define debug(x...) \
    do { \
        fprintf(stderr, MAG "[!] ERROR: " RESET); \
        fprintf(stderr, x); \
        fprintf(stderr, RESET); \
    } while(0)

#define ft_malloc(len,ptr) \
    do { \
        if(NULL == (ptr = malloc(len))){\
            fatal("Malloc failed\n"); \
        }\
    } while(0)

// Rotate byte b n bits left
#define rol8(b, n) ((b << n)|(b >> (8 - n)))
// Rotate byte b n bits right
#define ror8(b, n) ((b >> n)|(b << (8 - n)))

void dump_hex(const void* data, size_t size);

#endif
