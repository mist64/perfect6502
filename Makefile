OBJS=perfect6502.o netlist_sim.o
OBJS+=cbmbasic/cbmbasic.o cbmbasic/runtime.o cbmbasic/runtime_init.o cbmbasic/plugin.o cbmbasic/console.o cbmbasic/emu.o
CFLAGS=-Werror -Wall -O3
CC=cc

all: cbmbasic

cbmbasic: $(OBJS)
	$(CC) -o cbmbasic/cbmbasic $(OBJS)

benchmark: cbmbasic
	./cbmbasic/cbmbasic --benchmark

clean:
	rm -f $(OBJS) cbmbasic/cbmbasic

