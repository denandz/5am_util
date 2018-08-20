/*
*   File: main.c
*   Author: DoI
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <poll.h>
#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <fcntl.h>
#include <errno.h>    

#include "util.h"

int debug = 0;
int timeout = 50;

uint8_t checksum(uint8_t * bytes, uint64_t len){
    uint8_t cs = 0;
    uint64_t i;

    for(i = 0; i < len; i++){
        cs += bytes[i];
    }

    return cs;
}

// perform the fast init, returns 0 on success, -1 on failure
int fast_init(int fd){
    // Bit bang the fast init sequence using TIOCSBRK
    ioctl(fd, TIOCSBRK, 0x00);
    usleep(26000);
    ioctl(fd, TIOCCBRK, 0x00);
    usleep(25000);

    return 0;
}

uint32_t calc_key(uint32_t challenge){
    uint16_t * q = (uint16_t *)&challenge;
    uint16_t a = q[0];
    uint16_t w = (a >> 8)|(a << 8);
    uint8_t b1 = (w / 0xa1) & 0xff;
    uint8_t b2 = (q[1] % 0xc8) & 0xff;

    uint32_t response = (b1 << 24) | (b2 << 16) | (0x69 << 8) | 0x27; 
    
    return response;
}

int write_msg(int fd, uint8_t * msg, size_t len)
{
    int n = 0;
    uint8_t * buf;
    ft_malloc(len+1, buf);

    memcpy(buf, msg, len);
    buf[len] = checksum(buf, len);
    len++; 
    
    if(debug){
        printf(CYN);
        dump_hex(buf, len);
        printf(RESET);
    }

    n = write(fd, buf, len);
    if (n < 0){
        fatal("[!] write failed: %s", strerror(errno));
    }

    free(buf);
    return n;
}

int read_msg(int fd, uint8_t * buf, size_t len){
    int sel;
    struct pollfd pfd;
    pfd.fd = fd; 
    pfd.events = POLLIN;

    size_t i = 0;

    while((sel = poll(&pfd, 1, timeout)) != 0){
        if(i == len){
            return i;
        }

        ssize_t l = read(fd, buf+i, 1);
        if(l == -1){
            fatal("read failure");
        }
        
        i += l;
    }

    if(debug && i > 0){
        dump_hex(buf, i);
    }

    return i;
}

// Send the message, recieve the response, make sure that the response checksum is valid. 
// returns response length on success, or fatal()s on checksum failure.
int process_message(int fd, uint8_t * send_buf, size_t send_len, uint8_t * recv_buf, size_t recv_len){
    uint8_t * tmp_buf = 0x00;
    size_t rlen = 0, wlen = 0;
    
    if(recv_buf == 0x00){
        // allocate a temporary buffer
        ft_malloc(2048, tmp_buf);
        recv_buf = tmp_buf;
        recv_len = 2048;
    }

    wlen = write_msg(fd, send_buf, send_len);
    rlen = read_msg(fd, recv_buf, recv_len);

    // Adapter should respond with the message we sent, then any additional data from the ECU
    if(rlen <= wlen){
        fatal("Short read");
    }
    // calc the checksum
    uint8_t cs = checksum(recv_buf+wlen, rlen-wlen-1);

    // the last byte of the response should match the checksum 
    if(cs != recv_buf[rlen-1]){
        fatal("Checksum failed 0x%x != 0x%x", cs, recv_buf[rlen-1]);
    }

    if(tmp_buf){
        free(tmp_buf);
    }

    return rlen;
}

void help(){
    printf("5am_util - Firmware downloader for Marelli IAW5AM ECUs\n\n");
    printf("Usage - 5am_util -o dump.bin -i /dev/ttyUSB0\n");
    printf("-o <outfile>\tOutput file\n");
    printf("-i /dev/ttyUSB0\tSerial device - KKL 409.1 adapter connected to a powered-on ECU\n");
    printf("-v\t\tVerbose mode (includes full packet dumps)\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) 
{
    int fd, l;
    char * out_file = 0x00, * device = 0x00;
    uint8_t buf[4096];
    uint8_t memblob[0x4000];
    memset(memblob, 0xff, sizeof(memblob));

    // kwp2000 messages
    uint8_t init[] =    {0x81, 0x10, 0xF1, 0x81};
    uint8_t diag[] =    {0x82, 0x10, 0xF1, 0x10, 0x85};
    uint8_t info[] =      {0x82, 0x10, 0xF1, 0x1A, 0x80};  // get HW version and such
    uint8_t diag2[] =   {0x84, 0x10, 0x01, 0x10, 0x0C, 0x0C, 0x09};
    uint8_t login[] =    {0x82, 0x10, 0x01, 0x27, 0x01};
    uint8_t login_response[] =   {0x86, 0x10, 0x01, 0x27, 0x02, 0x00, 0x00, 0x00, 0x00};

    int opt;
    while ((opt = getopt(argc, argv, "h vt:o:i:")) != -1) {
        switch (opt) {
            case 'v':
                debug = 1;
                break;
            case 't':
                timeout = atoi(optarg);
                break;
            case 'o':
                out_file = optarg;
                break;
            case 'i':
                device = optarg;
                break;
            case 'h':
                help();
                break;
            default:
                help();
            }
    }

    if(!out_file|| !device){
        help();
    }

    printf("[+] 5am_util - begin\n");

    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);

    struct termios2 tio;
    ioctl(fd, TCGETS2, &tio);

    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = 10400;
    tio.c_ospeed = 10400;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag |= CREAD | CLOCAL;
    tio.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    tio.c_cflag &= ~(CSIZE|PARENB);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    int r = ioctl(fd, TCSETS2, &tio);
    if (r != 0) {
        fatal("ioctl: %s", strerror(errno));
    }

    fast_init(fd);

    l = read(fd, buf, sizeof(buf)); // perform an initial read
    if(debug){
        dump_hex(buf, l);
    }

    process_message(fd, init, sizeof(init), 0x00, 0x00);
    process_message(fd, diag, sizeof(diag), 0x00, 0x00);
    process_message(fd, info, sizeof(info), buf, sizeof(buf));

    char hw_ver[12];
    memset(hw_ver, 0x00, 12);
    memcpy(hw_ver, buf+22, 11);
    printf("Hardware Version:\t%s\n", hw_ver);

    process_message(fd, diag2, sizeof(diag2), 0x00, 0x00);

    // set new baudrate
    tio.c_ispeed = 64200;
    tio.c_ospeed = 64200;

    r = ioctl(fd, TCSETS2, &tio);
    if (r == 0) {
        printf(GRN "[+] BAUD RATE CHANGED\n" RESET);
    } else {
        fatal("ioctl");
    }

    int i = 0;
    write_msg(fd, login, sizeof(login));
    sleep(1); // give the device a chance to come up with a "random" seed
    l = read_msg(fd, buf, sizeof(buf));

    while(l != 17){
        if(i == 10){
            fatal("Could not retrieve challenge");
        }

        write_msg(fd, login, sizeof(login));
        sleep(1); // give the device a chance to come up with a "random" seed
        l = read_msg(fd, buf, sizeof(buf));

        i++;
    }

    uint32_t challenge = ntohl(*((uint32_t *) &buf[12]));
    printf("Challenge: \t" GRN "0x%x\n" RESET, challenge);
    uint32_t response = calc_key(challenge);
    printf("Response: \t" RED "0x%x\n" RESET, response);

    uint32_t ww = htonl(response);
    memcpy(&login_response[5], &ww, sizeof(uint32_t));
    process_message(fd, login_response, sizeof(login_response), buf, sizeof(buf));

    if(buf[14] == 0x67){
        printf(MAG "[+] Login successfull\n" RESET);
    } else{
        fatal("Login failed");
    }

    printf(GRN "[+] BEGINNING FIRMWARE DOWNLOAD\n" RESET);
    
    FILE * f = fopen(out_file, "w+");
    if(!f){
        fatal("Could not open file %s: %s", out_file, strerror(errno));
    }

    uint8_t block;
    for(block = 0; block < 20; block++){
        
        // we want blocks 0x00,0x01,0x06-0x12
        if(block > 1 && block < 6){
            fwrite(memblob, sizeof(uint8_t), 0x4000, f);
            continue;
        }

        uint8_t request_block[] = {0x87, 0x10, 0x01, 0x36, 0x11, 0x00, 0xFE, 0x02, 0x01, block}; 
        process_message(fd, request_block, sizeof(request_block), buf, sizeof(buf));

        char spinner[4] = "|/-\\";
        struct spint { unsigned i:2; } s;
        s.i=0;

        // each block is 16K, which we request in 32 byte chunks
        uint16_t offset;
        for(offset = 0x4000; offset < 0x8000 ; offset += 0x20){
            uint16_t off = htons(offset);
            uint8_t g[] = {0x86, 0x10, 0x01, 0x36, 0x21, 0x00, 0x00, 0x00, 0x20};
            memcpy(&g[6], &off, sizeof(uint16_t));

            l = process_message(fd, g, sizeof(g), buf, sizeof(buf));
            if(l < 53){
                fatal("partial block");
            }

            fwrite(buf+20, sizeof(uint8_t), l-21, f);
            fflush(f);
            printf("[%c] Block: %d\tOffset: 0x%x \r", spinner[s.i], block, offset-0x4000);
            fflush(stdout);
            s.i++;
        }
        putchar(0x0a);
    }
    putchar(0x0a);

    printf("[+] done - firmware dumped to %s\n", out_file);

    fclose(f);
    close(fd);
    return 0;
}
