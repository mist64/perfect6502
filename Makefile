OBJS=runtime.o runtime_init.o plugin.o perfect6502.o console.o emu.o
CFLAGS=-Wall -O3
CC=clang

all: cbmbasic

cbmbasic: $(OBJS)
	$(CC) -o cbmbasic $(OBJS)

clean:
	rm -f $(OBJS) cbmbasic

