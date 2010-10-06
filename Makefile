OBJS=perfect6502.o
#OBJS+=runtime.o runtime_init.o plugin.o console.o emu.o
OBJS+=measure.o
CFLAGS=-Wall -O3
CC=clang

all: cbmbasic

cbmbasic: $(OBJS)
	$(CC) -o cbmbasic $(OBJS)

clean:
	rm -f $(OBJS) cbmbasic

