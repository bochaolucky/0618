cmd_ipc/built-in.o :=  rm -f ipc/built-in.o; arm-none-eabi-ar rcSTPD ipc/built-in.o ipc/util.o ipc/msgutil.o ipc/msg.o ipc/sem.o ipc/shm.o ipc/syscall.o ipc/ipc_sysctl.o 
