cmd_kernel/printk/built-in.o :=  rm -f kernel/printk/built-in.o; arm-none-eabi-ar rcSTPD kernel/printk/built-in.o kernel/printk/printk.o kernel/printk/printk_safe.o 
