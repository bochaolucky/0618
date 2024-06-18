#!/bin/bash
export LD_LIBRARY_PATH=/home/boy/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi/lib:
export CROSS_COMPILE=arm-none-eabi-
export PATH=/home/boy/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi/bin:$PATH
export CROSS_COMPILE_ARM_PATH=/home/boy/gcc-arm-9.2-2019.12-x86_64-arm-none-eabi 
sudo make menuconfig CROSS_COMPILE=arm-none-eabi- ARCH=arm
sudo make CROSS_COMPILE=arm-none-eabi- ARCH=arm
mkdir /home/boy/Downloads/jiji/kernel
mv /home/boy/Downloads/jiji/linux_soc2018/vmlinux /home/boy/Downloads/jiji/kernel/vmlinux_big
mv /home/boy/Downloads/jiji/linux_soc2018/System.map /home/boy/Downloads/jiji/kernel/
mv /home/boy/Downloads/jiji/linux_soc2018/arch/arm/boot/zImage /home/boy/Downloads/jiji/kernel/
mv /home/boy/Downloads/jiji/linux_soc2018/arch/arm/boot/compressed/vmlinux /home/boy/Downloads/jiji/kernel/
cd /home/boy/Downloads/jiji/kernel
mkimage -A arm -O linux -T kernel -C none -a 0xC0000000 -e 0xC0000000 -n "Linux Kernel" -d zImage uImage
cd /home/boy/Downloads/jiji/scripts
g++ script.c
./a.out
g++ script2.c
./a.out
g++ script22.c
./a.out




