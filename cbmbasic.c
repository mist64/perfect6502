#include "perfect6502.h"

void init_monitor();
void handle_monitor();

int
main()
{
	int clk = 0;

	initAndResetChip();

	/* set up memory for user program */
	init_monitor();

	/* emulate the 6502! */
	for (;;) {
		step();
		clk = !clk;
		if (clk)
			handle_monitor();

		//chipStatus();
		//if (!(cycle % 1000)) printf("%d\n", cycle);
	};
}
