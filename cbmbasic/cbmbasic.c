#include <stdio.h>
#include "../perfect6502.h"
#include "runtime.h"
#include "runtime_init.h"
#include <time.h>


/*
See https://www.c64-wiki.com/wiki/C64-Commands

10 PRINT 200 * 25.4
20 PRINT 200 / 3.3333
30 END
RUN


Intel Xeon 3.4 Ghz
original speed: 19677 steps per second
original memory usage: 18.8 MB

current speed: 22018 steps per second
current memory usage: 1.1 MB

*/

int
main()
{
	int clk = 0;
 
    clock_t start_time, end_time;

	void *state = initAndResetChip();

	/* set up memory for user program */
	init_monitor();
    
    start_time = clock();

	/* emulate the 6502! */
	for (;;) {
		step(state);
		clk = !clk;
		if (clk)
			handle_monitor(state);

//		chipStatus(state);

		if ( (cycle % 20000) == 0 ) {
            end_time = clock();
            double time = (end_time - start_time)/ (double)(CLOCKS_PER_SEC);
            double speed = cycle / time;
            printf("cycle %u, speed %g steps per second\n", cycle, speed);
        }

	};
}
