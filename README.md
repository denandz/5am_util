# 5AM Util

5am_util is a firmware reader/writer utility targeted at Marelli IAW5AM ECUs, such as those found on the Ducati 848/1098/1198 bikes, among others. The tool talks to the ECU using KWP2000/UDS via a USB KKL adapter. This tool is a slightly-more-than-bare-bones proof-of-concept, and could probably benefit from things like reasonable ECU response code checking, error recovery and some code cleanup. Moving to a sensible UDS/KWP2000 library probably isn't a silly idea either.

## Hardware

This tool is intended to be used with a vag-com 409.1 KKL USB adapter. Testing was performed using multiple 5AM ECUs and a ~$4USD KKL interface on Ubuntu 18.04. No additional drivers were necessary. Double check the cable polarity before use.

## Usage

5am_util should be executed once the adapter is connected and the ECU is powered on. Both reading and writing should be done shortly after the ECU is powered on, with the ECU being powered off after the read or write is completed.

### Reading

```None
doi@buzdovan:~/src/5am_util$ ./5am_util -h
5am_util - Firmware downloader for Marelli IAW5AM ECUs

Usage - 5am_util -o dump.bin -i /dev/ttyUSB0
-o <outfile>    Output file
-i /dev/ttyUSB0 Serial device - KKL 409.1 adapter connected to a powered-on ECU
-v  Verbose mode (includes full packet dumps)
doi@buzdovan:~/src/5am_util$
doi@buzdovan:~/src/5am_util$
doi@buzdovan:~/src/5am_util$ sudo ./5am_util -o firmware.bin -i /dev/ttyUSB0 -v
[+] 5am_util - begin
00                                                |  .
81 10 F1 81 03                                    |  .....
81 10 F1 81 03 83 F1 10  C1 EF 8F C3              |  ............
82 10 F1 10 85 18                                 |  ......
82 10 F1 10 85 18 82 F1  10 50 85 58              |  .........P.X
82 10 F1 1A 80 1D                                 |  ......
82 10 F1 1A 80 1D BF F1  10 5A 80 39 36 35 31 38  |  .........Z.96518
34 30 37 42 20 20 49 41  57 35 41 4D 48 57 36 31  |  407B  IAW5AMHW61
30 00 32 32 33 35 53 46  30 31 20 20 20 00 00 35  |  0.2235SF01   ..5
41 4D 51 53 20 B0 0C 00  0C 9D 44 55 43 20 20 20  |  AMQS .....DUC
20 20 20 20 20 11 09 06  AC                       |       ....
Hardware Version:   IAW5AMHW610
84 10 01 10 0C 0C 09 C6                           |  ........
84 10 01 10 0C 0C 09 C6  80 01 10 01 50 E2        |  ............P.
[+] BAUD RATE CHANGED
82 10 01 27 01 BB                                 |  ...'..
82 10 01 27 01 BB 80 01  10 06 67 01 27 88 27 89  |  ...'......g.'.'.
5E                                                |  ^
Challenge:  0x27882789
Response:   0xda786927
86 10 01 27 02 DA 78 69  27 A2                    |  ...'..xi'.
86 10 01 27 02 DA 78 69  27 A2 80 01 10 03 67 02  |  ...'..xi'.....g.
34 31                                             |  41
[+] Login successfull
[+] BEGINNING FIRMWARE DOWNLOAD
87 10 01 36 11 00 FE 02  01 00 E0                 |  ...6.......
87 10 01 36 11 00 FE 02  01 00 E0 80 01 10 03 76  |  ...6...........v
11 02 1D                                          |  ...
86 10 01 36 21 00 40 00  20 4E                    |  ...6!.@. N
86 10 01 36 21 00 40 00  20 4E 80 01 10 26 76 21  |  ...6!.@. N...&v!
40 00 00 20 FA 00 00 02  FA 00 04 40 FA 00 08 40  |  @.. .......@...@
FA 00 0C 40 FA 00 10 40  FA 00 14 40 FA 00 18 40  |  ...@...@...@...@
FA 00 1C 40 B0                                    |  ...@.
{..snip..}
```

### Writing

Writing will load the provided firmware file, encrypt the firmware into the format expected by the ECU and upload. The util will display the first 64 bytes of the firmware file you've provided, so double check that looks sane before hitting enter. I'd expect to see the interrupt table (FA00xxxx) at the files start.

```None
doi@buzdovan:~/src/5am_util$ sudo ./5am_util -w ./848.bin -i /dev/ttyUSB0
[!] Will write firmware: ./848.bin [!]
[!] Checksum: 9973 [!]
[+] Displaying first 64 bytes of the bin file

FA 00 00 02 FA 00 04 40  FA 00 08 40 FA 00 0C 40  |  .......@...@...@ 
FA 00 10 40 FA 00 14 40  FA 00 18 40 FA 00 1C 40  |  ...@...@...@...@ 
FA 00 20 40 FA 00 24 40  FA 00 28 40 FA 00 2C 40  |  .. @..$@..(@..,@ 
FA 00 30 40 FA 00 34 40  FA 00 38 40 FA 00 3C 40  |  ..0@..4@..8@..<@ 

Does the above look right to you? Last chance to hit ctrl-c. Any key to continue

[+] 5am_util - begin
Hardware Version:    IAW5AMHW610
[+] BAUD RATE CHANGED
Challenge:  0x3CA93CAA
Response:   0xE816927
[+] Login successfull
[+] Erasing
[+] Beginning firmware upload
[/] Writing 311150 of 311304

[+] Transfer complete

[+] Programming...

[+] Writing complete

```

### Bench Flashing

The IAW5AM ECUs can be read while connected to a bench power supply. Use the following pin-out on the ECU body connector (the side with the bolts that are closer together)

* 12V - Pin 4, 17
* Ground - Pin 10, 34
* K-Line - Pin 16

### Flashing on-bike

Firmware downloads take ~20 minutes, so something like a battery tender is probably a good idea if the ECU is still attached to the rest of the motorcycle.

## Tested ECUs

So far this util has successfully downloaded firmware from the following bike ECUs:

* Ducati 848
* Ducati Multistrada
* Ducati SC1000

In theory, this should work with any Marelli IAW5AM ECU. If you confirm this tool on a bike other than the above let me know and I'll add it to the list.

## KWP2000/UDS

The 5AM uses KWP2000 (ISO 14230) and UDS (Unified Diagnostic Services - ISO 14229-1). The message structure is defined in ISO 14230-2. You can read more about UDS [here](https://en.wikipedia.org/wiki/Unified_Diagnostic_Services), which might shed some light on the messages sent to the ECU.

## Warning

This tool is designed to allow you to download, tamper and reflash ECU firmware. There is a risk of bricking your ECU if/when something goes horribly, horribly wrong. I take no responsibility for the havoc that may befall your ECU.
