cmd_kernel/rcu/built-in.o :=  rm -f kernel/rcu/built-in.o; arm-none-eabi-ar rcSTPD kernel/rcu/built-in.o kernel/rcu/update.o kernel/rcu/sync.o kernel/rcu/srcutiny.o kernel/rcu/tiny.o 
