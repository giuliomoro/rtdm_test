#!/bin/bash
#remove uio_pruss and enable stuff the PRU.
#Run this file if you want to use memmap to load the PRU code.

rmmod uio_pruss >/dev/null
devmem2 0x44e00140 b 0x0 #enable PRU clock transition
devmem2 0x44e000e8 b 0x2 #enables the clock
devmem2 0x44e00c00 b 0x0 #clear bit 1
devmem2 0x4a326000

 
