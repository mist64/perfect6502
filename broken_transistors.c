
#define MAX_CYCLES 33000
BOOL log_rw[MAX_CYCLES];
uint16_t log_ab[MAX_CYCLES];
uint8_t log_db[MAX_CYCLES];
int
main()
{
	setupNodesAndTransistors();
	for (int run = -1; run < transistors; run ++) {
#if 0
		/* skip a few runs! */
		if (run == 0)
			run = 180;
#endif

		if (run != -1) {
			printf("testing transistor %d: ", run);
			broken_transistor = run;
		}

		bzero(memory, 65536);
		init_monitor();
		resetChip();
		BOOL fail = NO;
		for (int c = 0; c < MAX_CYCLES; c++) {
			step();
			if (run == -1) {
				log_rw[c] = isNodeHigh(rw);
				log_ab[c] = readAddressBus();
				log_db[c] = readDataBus();
			} else {
				if (log_rw[c] != isNodeHigh(rw)) {
					printf("FAIL, RW %d instead of %d @ %d\n", isNodeHigh(rw), log_rw[c], c);
					fail = YES;
					break;
				}
				if (log_ab[c] != readAddressBus()) {
					printf("FAIL, AB 0x%04x instead of 0x%04x @ %d\n", readAddressBus(), log_ab[c], c);
					fail = YES;
					break;
				}
				if (log_db[c] != readDataBus()) {
					printf("FAIL, DB 0x%02x instead of 0x%02x @ %d\n", readDataBus(), log_db[c], c);
					fail = YES;
					break;
				}
			}
		}
		if (run != -1 && !fail)
			printf("PASS\n");
	}
}
