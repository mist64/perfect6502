#include <stdio.h>
#ifndef _WIN32
#include <sys/stat.h>
#else
#include <io.h>
#endif
#include "../perfect6502.h"
 
 state_t *state;

/************************************************************
 *
 * Interface to OS Library Code / Monitor
 *
 ************************************************************/

/* imported by runtime.c */
unsigned char A, X, Y, S, P;
unsigned short PC;
int N, Z, C;

int
init_monitor()
{
	FILE *f;
	f = fopen("apple1basic/apple1basic.bin", "rb");
	if (f == NULL) {
		perror("Error opening ROM image");
		return 1;
	}
	if (fread(memory + 0xE000, 1, 4096, f) != 4096) {
		perror("Error reading ROM image");
		return 1;
	}
	fclose(f);

	memory[0xfffc] = 0x00;
	memory[0xfffd] = 0xE0;
	return 0;
}


void
charout(char ch) {
	unsigned char S = readSP(state);
	unsigned short a = (1 + memory[0x0100+S+1]) | memory[0x0100+((S+2) & 0xFF)] << 8;

	/*
	 * Apple I BASIC prints every character received
	 * from the terminal. UNIX terminals do this
	 * anyway, so we have to avoid printing every
	 * line again
	 */
	if (a==0xe2a6)	/* character echo */
		return;
	if (a==0xe2b6)	/* CR echo */
		return;

	/*
	 * Apple I BASIC prints a line break and 6 spaces
	 * after every 37 characters. UNIX terminals do
	 * line breaks themselves, so ignore these
	 * characters
	 */
	if (a==0xe025 && (ch==10 || ch==' '))
		return;

	/* INPUT */
	if (a==0xe182) {
#if _WIN32
		if (!_isatty(0))
			return;
#else
		struct stat st;
		fstat(0, &st);
		if (S_ISFIFO (st.st_mode))
			return;
#endif
	}

	putc(ch, stdout);
	fflush(stdout);
}

void
handle_monitor()
{
	if (readRW(state)) {
		unsigned short a = readAddressBus(state);
		if ((a & 0xFF1F) == 0xD010) {
			unsigned char c = getchar();
			if (c == 10)
				c = 13;
			c |= 0x80;
			writeDataBus(state, c);
		}
		if ((a & 0xFF1F) == 0xD011) {
			if (readPC(state) == 0xE006)
				/* if the code is reading a character, we have one ready */
				writeDataBus(state, 0x80);
			else
				/* if the code checks for a STOP condition, nothing is pressed */
				writeDataBus(state, 0);
		}
		if ((a & 0xFF1F) == 0xD012) {
			/* 0x80 would mean we're not yet ready to receive a character */
			writeDataBus(state, 0);
		}
	} else {
		unsigned short a = readAddressBus(state);
		unsigned char d = readDataBus(state);
		if ((a & 0xFF1F) == 0xD012) {
			unsigned char temp8 = d & 0x7F;
			if (temp8 == 13)
				temp8 = 10;
			charout(temp8);
		}
	}
}

int
main(int argc, char **argv)
{
	int clk = 0;

	state = initAndResetChip();

	/* set up memory for user program */
	if (init_monitor()) {
		return 1;
	}

	/* emulate the 6502! */
	for (;;) {
		step(state);
		clk = !clk;
		if (!clk)
			handle_monitor();

		chipStatus(state);
		//if (!(cycle % 1000)) printf("%d\n", cycle);
	};
}
