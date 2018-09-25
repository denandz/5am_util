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
#include "fw.h"

int debug = 0;
int timeout = 50;
struct termios2 tio;

uint8_t checksum8(uint8_t * bytes, uint64_t len){
    uint8_t cs = 0;
    uint64_t i;

    for(i = 0; i < len; i++){
        cs += bytes[i];
    }

    return cs;
}

uint16_t checksum16(uint8_t * bytes, uint64_t len){
    uint16_t cs = 0;
    uint64_t i;

    for(i = 0; i < len; i++){
        cs += bytes[i];
    }   

    return cs; 
}

// The uploaded firmware needs to be "encrypted" using this funky combination of addition and right-rolling. 
void encrypt_blob(uint8_t * buf, uint64_t len){
    uint64_t i = 0, processed = 0; 

    while(processed < len){
        for(i = 0; i < 8; i++){
            switch(i){
                case 0:
                    buf[processed] += 0x88;
                    buf[processed] = ~ ror8(buf[processed], 1);
                    break;

                case 1:
                    buf[processed] += 0xC7;
                    buf[processed] = ror8(buf[processed], 1);
                    break;

                case 2:
                    buf[processed] += 0x26;
                    buf[processed] = ~ ror8(buf[processed], 3);
                    break;

                case 3:
                    buf[processed] += 0xA5;
                    buf[processed] = ~ ror8(buf[processed], 5);
                    break;

                case 4:
                    buf[processed] += 0x6C;
                    buf[processed] = ror8(buf[processed], 2);
                    break;

                case 5:
                    buf[processed] += 0xEB;
                    buf[processed] = ror8(buf[processed], 6);
                    break;

                case 6:
                    buf[processed] += 0xA;
                    buf[processed] = ~ ror8(buf[processed], 6);
                    break;

                case 7:
                    buf[processed] = 0x66+(buf[processed] * -1);
                    buf[processed] = ror8(buf[processed], 4);
                    break;
            }

            processed++;
        }
    }
}

// read the file at path and load into a firmware struct, fatal if lengths are wrong
void load_bin(char * path, struct fw * firmware){
    long file_length;
    size_t r;

    FILE * fp = fopen(path, "r");
    if(!fp){
        fatal("fopen %s: %s", path, strerror(errno));
    }
   
    if(fseek(fp, 0L, SEEK_END) < 0){
        fatal("fseek: %s", strerror(errno));
    }

    file_length = ftell(fp);
    if(file_length < 0){
        fatal("ftell: %s", strerror(errno));
    }
    else if(file_length != 327680){
        fatal("Firmware image length is incorrect (%lu), should be 327680 bytes", file_length);
    }

    rewind(fp);

    ft_malloc(0x50000, firmware->bin);
    memset(firmware->bin, 0x00, 0x50000);
    r = fread(firmware->bin, sizeof(uint8_t), 327680, fp); 
    if(r != 327680){
        fatal("Error: short fread. Firmware image should be 327680 bytes");
    }

    if(debug){
        printf("[+] Read %lu bytes from %s\n", r, path);
    }
    firmware->bin_len = r;
    firmware->enc_len = r-0x3FF8;

    // calculate the firmware checksum
    firmware->checksum = checksum16(firmware->bin+0x4000, 0x4BFFE);

    ft_malloc(firmware->enc_len, firmware->enc);
    firmware->enc[0] = 0xC2;
    firmware->enc[1] = 0x07;
    firmware->enc[2] = 0x16;
    firmware->enc[3] = 0x33;
    firmware->enc[4] = 0x6F;
    firmware->enc[5] = 0xEB;
    firmware->enc[6] = 0xB0;
    firmware->enc[7] = 0x1D;

    memcpy(firmware->enc+8, firmware->bin+0x4000, firmware->enc_len-8);
    encrypt_blob(firmware->enc, firmware->enc_len);
    if(debug){
        printf("[+] First 128 bytes of the encrypted bin:\n");
        dump_hex(firmware->enc, 128);
    }

    fclose(fp);
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

void set_baudrate(int fd, int baud){
    int r;
    tio.c_ispeed = baud;
    tio.c_ospeed = baud;

    r = ioctl(fd, TCSETS2, &tio);
    if (r == 0) {
        printf(GRN "[+] BAUD RATE CHANGED\n" RESET);
    } else {
        fatal("ioctl");
    }
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

int write_msg(int fd, uint8_t * msg, size_t len){
    int n = 0;
    uint8_t * buf;
    ft_malloc(len+1, buf);

    memcpy(buf, msg, len);
    buf[len] = checksum8(buf, len);
    len++; 
    
    if(debug){
        printf(CYN);
        dump_hex(buf, len);
        printf(RESET);
    }

    n = write(fd, buf, len);
    if (n < 0){
        fatal("write failed: %s", strerror(errno));
    }

    free(buf);
    return n;
}

int read_msg(int fd, uint8_t * buf, size_t len){
    struct pollfd pfd;
    pfd.fd = fd; 
    pfd.events = POLLIN;

    size_t i = 0;

    while(poll(&pfd, 1, timeout) != 0){
        if(i == len){
            break; 
        }

        ssize_t l = read(fd, buf+i, 1);
        if(l < 0){
            fatal("read failed: %s", strerror(errno));
        }
        
        i += l;
    }

    if(debug && i > 0){
        dump_hex(buf, i);
    }

    return i;
}

// Send the message, receive the response, make sure that the response checksum is valid. 
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
    // calculate the checksum
    uint8_t cs = checksum8(recv_buf+wlen, rlen-wlen-1);

    // the last byte of the response should match the checksum 
    if(cs != recv_buf[rlen-1]){
        fatal("Checksum failed 0x%x != 0x%x", cs, recv_buf[rlen-1]);
    }

    if(tmp_buf){
        free(tmp_buf);
    }

    return rlen;
} 

void login(int fd){
    uint8_t security_access[] =    {0x82, 0x10, 0x01, 0x27, 0x01};
    uint8_t login_response[] =   {0x86, 0x10, 0x01, 0x27, 0x02, 0x00, 0x00, 0x00, 0x00};

    uint8_t buf[4096];
    memset(buf, 0x00, sizeof(buf));

    int i = 0, l = 0;

    do {
        if(i == 10){
            fatal("Could not retrieve challenge");
        }

        write_msg(fd, security_access, sizeof(security_access));
        sleep(1); // give the device a chance to come up with a "random" seed
        l = read_msg(fd, buf, sizeof(buf));

        i++;
    } while(buf[10] == 0x7F || l != 17);

    uint32_t challenge = buf[12] << 24| buf[13] << 16 | buf[14] << 8 | buf[15]; 
    printf("Challenge: \t" GRN "0x%X\n" RESET, challenge);
    uint32_t response = calc_key(challenge);
    printf("Response: \t" RED "0x%X\n" RESET, response);

    uint32_t ww = htonl(response);
    memcpy(&login_response[5], &ww, sizeof(uint32_t));
    process_message(fd, login_response, sizeof(login_response), buf, sizeof(buf));

    if(buf[14] == 0x67){
        printf(MAG "[+] Login successful\n" RESET);
    } else{
        fatal("Login failed");
    }
}

void write_firmware(int fd, struct fw * firmware){
        uint8_t buf[8192];

        if(firmware->enc_len != 0x4C008){
            fatal("Encrypted length is incorrect");
        }

        // start diag
        uint8_t diag_03[] = {0x83, 0x10, 0xF1, 0x10, 0x85, 0x03};
        process_message(fd, diag_03, sizeof(diag_03), 0x00, 0x00);

        set_baudrate(fd, 38400);

        // access timing params
        uint8_t timing[] = {0x87, 0x10, 0xF1, 0x83, 0x03, 0x1E, 0x02, 0x0A, 0x14, 0x00};
        process_message(fd, timing, sizeof(timing), 0x00, 0x00);

        login(fd);

        // If you dont send the writer and date info, then the write_init will fail!
        // send writer
        uint8_t set_writer[] = {0x83, 0x10, 0xF1, 0x3B, 0x98, 0x20};
        process_message(fd, set_writer, sizeof(set_writer), 0x00, 0x00);

        // send date (20180101 here)
        uint8_t set_writedate[] = {0x86, 0x10, 0xF1, 0x3B, 0x99, 0x20, 0x18, 0x01, 0x01};
        process_message(fd, set_writedate, sizeof(set_writedate), 0x00, 0x00);

        printf("[+] Erasing\n");

        // Call the erase routine (0x02)
        uint8_t erase_routine[] = {0x88, 0x10, 0xF1, 0x31, 0x02, 0x00, 0x40, 0x00, 0x04, 0xFF, 0xFF};
        process_message(fd, erase_routine, sizeof(erase_routine), 0x00, 0x00);

        // erase
        uint8_t erase[] = {0x82, 0x10, 0xF1, 0x33, 0x02};
        write_msg(fd, erase, sizeof(erase));

        // give the ECU a chance to complete the erase.
        // This is a hacky way to deal with the erase status messages that get sent back by the ECU
        sleep(10);
        read_msg(fd, buf, sizeof(buf)); 

        printf("[+] Beginning firmware upload\n");
        uint8_t request_write[] = {0x88, 0x10, 0xF1, 0x34, 0x00, 0x40, 0x00, 0x33, 0x04, 0xC0, 0x00};
        process_message(fd, request_write, sizeof(request_write), 0x00, 0x00);

        uint64_t i = 0;
        int n = 1;
        uint8_t write_chunk[260];

        char spinner[4] = "|/-\\";
        struct spint { unsigned i:2; } s;
        s.i=0;

        while(i < firmware->enc_len){
            printf("[%c] Writing %ld of %ld\r", spinner[s.i], i, firmware->enc_len);
            fflush(stdout);
            s.i++;
            
            uint8_t chunk_len = firmware->enc_len-i < 254 ? firmware->enc_len-i : 254;

            memset(write_chunk, 0x00, sizeof(write_chunk));
            write_chunk[0] = 0x80;
            write_chunk[1] = 0x10;
            write_chunk[2] = 0xF1;
            write_chunk[3] = chunk_len+1;
            write_chunk[4] = 0x36;

            memcpy(write_chunk+5, firmware->enc+i, chunk_len);
            if(debug){
                printf("[.] Chunk %d size: %d\n", n, chunk_len);
            }

            process_message(fd, write_chunk, chunk_len+5, 0x00, 0x00);
            i += chunk_len;
            n++;
        }
        printf("\n");
 
        uint8_t transfer_exit[] = {0x80, 0x10, 0xF1, 0x01, 0x37};
        process_message(fd, transfer_exit, sizeof(transfer_exit), 0x00, 0x00);
        printf("[+] Transfer complete\n");

        printf("[+] Programming...\n");
        // Call the routine to program the ECU, sending the firmware checksum
        uint8_t program_routine[] = {0x80, 0x10, 0xF1, 0x0A, 0x31, 0x01, 0x00, 0x40, 0x00, 0x04, 0xFF, 0xFF, 0x00, 0x00};
        program_routine[12] = firmware->checksum >> 8;
        program_routine[13] = firmware->checksum & 0xff;
        process_message(fd, program_routine, sizeof(program_routine), 0x00, 0x00);
       
        uint8_t program[] = {0x82, 0x10, 0xF1, 0x33, 0x01};
        process_message(fd, program, sizeof(program), 0x00, 0x00);
        sleep(2);

        printf("[+] Write complete\n");
} 

void help(){
    printf("5am_util - Firmware reader/writer for Marelli IAW5AM ECUs\n\n");
    printf("Usage - 5am_util -o dump.bin -i /dev/ttyUSB0\n");
    printf("-o <outfile>\tOutput file\n");
    printf("-i /dev/ttyUSB0\tSerial device - KKL 409.1 adapter connected to a powered-on ECU\n");
    printf("Writer usage - 5am_util -w firmware.bin -i /dev/ttyUSB0\n");
    printf("-w <bin file>\t\tFirmware image to write\n");
    printf("-v\t\tVerbose mode (includes full packet dumps)\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]){
    int fd, l, r;
    char * out_file = 0x00, * device = 0x00, * bin_file = 0x00;
    uint8_t buf[4096];
    uint8_t memblob[0x4000];
    memset(memblob, 0xff, sizeof(memblob));

    struct fw * firmware = 0x00;

    // kwp2000 messages
    uint8_t init[] =    {0x81, 0x10, 0xF1, 0x81};
    uint8_t diag[] =    {0x82, 0x10, 0xF1, 0x10, 0x85};
    uint8_t info[] =      {0x82, 0x10, 0xF1, 0x1A, 0x80};  // get HW version and such
    uint8_t diag2[] =   {0x84, 0x10, 0x01, 0x10, 0x0C, 0x0C, 0x09};

    int opt;
    while ((opt = getopt(argc, argv, "h vt:o:i:w:")) != -1) {
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
            case 'w':
                bin_file = optarg;
                break;
            default:
                help();
            }
    }

    if(!(out_file||bin_file) || !device || (out_file && bin_file)){
        help();
    }

    if(bin_file){
        ft_malloc(sizeof(struct fw), firmware);
        load_bin(bin_file, firmware);

        printf(RED "[!]" RESET " Will write firmware: %s " RED "[!]\n" RESET, bin_file);
        printf(RED "[!]" RESET " Checksum: %x " RED "[!]\n" RESET, firmware->checksum);
        printf("[+] Displaying first 64 bytes of the bin file\n\n");

        dump_hex(firmware->bin, 64);
        printf("\nDoes the above look right to you? Last chance to hit ctrl-c. Any key to continue\n");
        getchar();
    }

    printf("[+] 5am_util - begin\n");

    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(!fd){
        fatal("Could not open device %s: %s", device, strerror(errno));
    }

    r = ioctl(fd, TCGETS2, &tio);
    if (r != 0) {
        fatal("ioctl: %s", strerror(errno));
    }

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

    r = ioctl(fd, TCSETS2, &tio);
    if (r != 0) {
        fatal("ioctl: %s", strerror(errno));
    }

    fast_init(fd);

    l = read(fd, buf, sizeof(buf)); // perform an initial read
    if(l < 0){
        fatal("read failed: %s", strerror(errno));
    }
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


    if(bin_file){
        write_firmware(fd, firmware);
    }
    else{
        process_message(fd, diag2, sizeof(diag2), 0x00, 0x00);

        set_baudrate(fd, 64200);
        login(fd);

        printf(GRN "[+] BEGINNING FIRMWARE DOWNLOAD\n" RESET);
    
        FILE * f = fopen(out_file, "w+");
        if(!f){
            fatal("fopen %s: %s", out_file, strerror(errno));
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
    }

    if(firmware){
        if(firmware->bin){
            free(firmware->bin);
        }

        if(firmware->enc){
            free(firmware->enc);
        }

        free(firmware);
    }


    close(fd);
    return 0;
}
