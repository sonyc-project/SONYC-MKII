#!/bin/bash

TEMP_SREC=build/temp.srec
OUT_COMB=build/combined.srec
OUT_FILE=build/combined.elf
OUT_HEX=build/combined.hex

if [ -z $1 ]
then
	echo "No input file."
	exit 1
fi

arm-none-eabi-objcopy -O srec $1 $TEMP_SREC
srec_cat build/h7boot.srec $TEMP_SREC -o $OUT_COMB
arm-none-eabi-objcopy -O elf32-little -I srec $OUT_COMB $OUT_FILE
arm-none-eabi-objcopy -O ihex -I srec $OUT_COMB $OUT_HEX
echo Wrote $OUT_FILE
echo Done
