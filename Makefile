OBJS=perfect6502.o
OBJS+=cbmbasic.o runtime.o runtime_init.o plugin.o console.o emu.o
#OBJS+=measure.o
#OBJS+=broken_transistors.o runtime.o runtime_init.o plugin.o console.o emu.o
CFLAGS=-Werror -Wall -O3
#CFLAGS+=-DBROKEN_TRANSISTORS
CC=clang

all: cbmbasic

cbmbasic: $(OBJS)
	$(CC) -o cbmbasic $(OBJS)

clean:
	rm -f $(OBJS) cbmbasic

