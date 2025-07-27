#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../perfect6502.h"
/* XXX hook up memory[] with RAM[] in runtime.c */

extern int benchmark_mode;
extern unsigned long cycle;
static clock_t benchmark_start_time;
 
/************************************************************
 *
 * Interface to OS Library Code / Monitor
 *
 ************************************************************/

extern int kernal_dispatch(void);

/* imported by runtime.c */
unsigned char A, X, Y, S, P;
unsigned short PC;
int N, Z, C;

int
init_monitor()
{
	FILE *f;
	f = fopen("cbmbasic/cbmbasic.bin", "rb");
	if (f == NULL) {
		perror("Error opening cbmbasic/cbmbasic.bin");
		return 1;
	}
	size_t readlen = fread(memory + 0xA000, 1, 17591, f);
	fclose(f);
	if (readlen != 17591) {
		perror("Error reading cbmbasic/cbmbasic.bin");
		return 1;
	}

	if (benchmark_mode) {
		benchmark_start_time = clock();
	}

	/*
	 * fill the KERNAL jumptable with JMP $F800;
	 * we will put code there later that loads
	 * the CPU state and returns
	 */
	for (unsigned short addr = 0xFF90; addr < 0xFFF3; addr += 3) {
		memory[addr+0] = 0x4C;
		memory[addr+1] = 0x00;
		memory[addr+2] = 0xF8;
	}

	/*
	 * cbmbasic scribbles over 0x01FE/0x1FF, so we can't start
	 * with a stackpointer of 0 (which seems to be the state
	 * after a RESET), so RESET jumps to 0xF000, which contains
	 * a JSR to the actual start of cbmbasic
	 */
	memory[0xf000] = 0x20;
	memory[0xf001] = 0x94;
	memory[0xf002] = 0xE3;
	
	memory[0xfffc] = 0x00;
	memory[0xfffd] = 0xF0;
	return 0;
}

void
handle_monitor(void *state)
{
	PC = readPC(state);

	if (PC == 0xFFCF && benchmark_mode) {
		clock_t end_time = clock();
		double elapsed_time = (double)(end_time - benchmark_start_time) / CLOCKS_PER_SEC;
		double cycles_per_sec = cycle / elapsed_time;

		printf("Benchmark results:\n");
		printf("  Half-cycles: %lu\n", cycle);
		printf("  Time: %.3f seconds\n", elapsed_time);
		printf("  Performance: %.0f cycles/sec\n", cycles_per_sec);
		chipStatus(state);
		exit(0);
	}

	if (PC >= 0xFF90 && ((PC - 0xFF90) % 3 == 0)) {
		/* get register status out of 6502 */
		A = readA(state);
		X = readX(state);
		Y = readY(state);
		S = readSP(state);
		P = readP(state);
		N = P >> 7;
		Z = (P >> 1) & 1;
		C = P & 1;

		kernal_dispatch();

		/* encode processor status */
		P &= 0x7C; /* clear N, Z, C */
		P |= (N << 7) | (Z << 1) | C;

		/*
		 * all KERNAL calls make the 6502 jump to $F800, so we
		 * put code there that loads the return state of the
		 * KERNAL function and returns to the caller
		 */
		memory[0xf800] = 0xA9; /* LDA #P */
		memory[0xf801] = P;
		memory[0xf802] = 0x48; /* PHA    */
		memory[0xf803] = 0xA9; /* LHA #A */
		memory[0xf804] = A;
		memory[0xf805] = 0xA2; /* LDX #X */
		memory[0xf806] = X;
		memory[0xf807] = 0xA0; /* LDY #Y */
		memory[0xf808] = Y;
		memory[0xf809] = 0x28; /* PLP    */
		memory[0xf80a] = 0x60; /* RTS    */
		/*
		 * XXX we could do RTI instead of PLP/RTS, but RTI seems to be
		 * XXX broken in the chip dump - after the KERNAL call at 0xFF90,
		 * XXX the 6502 gets heavily confused about its program counter
		 * XXX and executes garbage instructions
		 */
	}
}

