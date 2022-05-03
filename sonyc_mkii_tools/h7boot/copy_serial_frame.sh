#!/bin/bash

C_DIR=../../MF44/eMote44/DeviceCode/pal/Samraksh/serial_frame
H_DIR=../../MF44/eMote44/DeviceCode/include/Samraksh
MEL_DIR=../master_mel

if [ ! -d $C_DIR ]
then
mkdir $C_DIR
fi

if [ ! -d $H_DIR ]
then
mkdir $H_DIR
fi

cp Core/src/serial_frame.c $C_DIR
cp Core/inc/serial_frame.h $C_DIR

cp Core/src/serial_frame.c $MEL_DIR
cp Core/inc/serial_frame.h $MEL_DIR
