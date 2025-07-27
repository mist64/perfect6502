#include <stdio.h>
#include <string.h>
#include "../perfect6502.h"
#include "runtime.h"
#include "runtime_init.h"
#include <time.h>

int benchmark_mode = 0;


/*
See https://www.c64-wiki.com/wiki/C64-Commands

10 PRINT 200 * 25.4
20 PRINT 200 / 3.3333
30 END
RUN

*/

#define SHOW_AVG_SPEED      0

int
main(int argc, char *argv[])
{
	int clk = 0;

	if (argc > 1 && strcmp(argv[1], "--benchmark") == 0)
		benchmark_mode = 1;
 
	void *state = initAndResetChip();

	/* set up memory for user program */
	if (init_monitor()) {
		return 1;
	}

#if SHOW_AVG_SPEED
    clock_t end_time;
    clock_t start_time = clock();
#endif

	/* emulate the 6502! */
	for (;;) {
		step(state);
		clk = !clk;
		if (clk)
			handle_monitor(state);

/*		chipStatus(state);
*/

#if SHOW_AVG_SPEED
		if ( (cycle % 20000) == 0 ) {
            end_time = clock();
            double time = (end_time - start_time)/ (double)(CLOCKS_PER_SEC);
            double speed = cycle / time;
            printf("cycle %lu, speed %g steps per second\n", cycle, speed);
        }
#endif

	}   /* end infinite loop */

}
