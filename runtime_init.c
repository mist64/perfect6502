#include <stdio.h>

extern unsigned char memory[65536]; /* XXX must be hooked up with RAM[] in runtime.c */

extern unsigned short readPC();
extern unsigned char readA();
extern unsigned char readX();
extern unsigned char readY();
extern unsigned char readSP();
extern unsigned char readP();

/************************************************************
 *
 * Interface to OS Library Code / Monitor
 *
 ************************************************************/

extern int kernal_dispatch();

/* imported by runtime.c */
unsigned char A, X, Y, S, P;
unsigned short PC;
int N, Z, C;

void
init_monitor()
{
	FILE *f;
	f = fopen("cbmbasic.bin", "r");
	fread(memory + 0xA000, 1, 17591, f);
	fclose(f);

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
}

void
handle_monitor()
{
	PC = readPC();

	if (PC >= 0xFF90 && ((PC - 0xFF90) % 3 == 0)) {
		/* get register status out of 6502 */
		A = readA();
		X = readX();
		Y = readY();
		S = readSP();
		P = readP();
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

