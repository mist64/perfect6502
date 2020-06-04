#include "../perfect6502.h"
#include "runtime.h"
#include "runtime_init.h"

int
main()
{
	int clk = 0;

	void *state = initAndResetChip();

	/* set up memory for user program */
	init_monitor();

	/* emulate the 6502! */
	for (;;) {
		step(state);
		clk = !clk;
		if (clk)
			handle_monitor(state);

//		chipStatus(state);
		//if (!(cycle % 1000)) printf("%d\n", cycle);
	};
}
