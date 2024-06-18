cmd_kernel/debug/built-in.o :=  rm -f kernel/debug/built-in.o; arm-none-eabi-ar rcSTPD kernel/debug/built-in.o kernel/debug/debug_core.o kernel/debug/gdbstub.o 
