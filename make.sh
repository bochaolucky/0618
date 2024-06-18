#!/bin/bash
export LD_LIBRARY_PATH=/home/neu/Downloads/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi/lib:
export CROSS_COMPILE=arm-none-eabi-
export PATH=/home/neu/Downloads/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi/arm-none-eabi/bin:$PATH
export CROSS_COMPILE_ARM_PATH=/home/neu/Downloads/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi 
sudo make menuconfig CROSS_COMPILE=arm-none-eabi- ARCH=arm 
sudo make CROSS_COMPILE=arm-none-eabi- ARCH=arm
/home/neu/Downloads/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi/bin/arm-none-eabi-objdump -D vmlinux > /home/neu/new.txt
