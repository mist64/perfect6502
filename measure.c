#include <stdio.h>
#include <strings.h>

#include "perfect6502.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int BOOL;

extern uint8_t memory[65536];

#define YES 1
#define NO 0

#define BRK_LENGTH 2 /* BRK pushes PC + 2 onto the stack */

#define MAX_CYCLES 100
#define SETUP_ADDR 0xF400
#define INSTRUCTION_ADDR 0xF800
#define BRK_VECTOR 0xFC00

#define MAGIC_8 0xEA
#define MAGIC_16 0xAB1E
#define MAGIC_IZX 0x1328
#define MAGIC_IZY 0x1979
#define X_OFFSET 5
#define Y_OFFSET 10

#define IS_READ_CYCLE ((cycle & 1) && readRW(state))
#define IS_WRITE_CYCLE ((cycle & 1) && !readRW(state))
#define IS_READING(a) (IS_READ_CYCLE && readAddressBus(state) == (a))

struct {
	BOOL crash;
	int length;
	int cycles;
	int addmode;
	BOOL zp;
	BOOL abs;
	BOOL zpx;
	BOOL absx;
	BOOL zpy;
	BOOL absy;
	BOOL izx;
	BOOL izy;
	BOOL reads;
	BOOL writes;
	BOOL inputa;
	BOOL inputx;
	BOOL inputy;
	BOOL inputs;
	BOOL inputp;
	BOOL outputa;
	BOOL outputx;
	BOOL outputy;
	BOOL outputs;
	BOOL outputp;
} data[256];

enum {
	ADDMODE_UNKNOWN,
	ADDMODE_IZY,
	ADDMODE_IZX,
	ADDMODE_ZPY,
	ADDMODE_ZPX,
	ADDMODE_ZP,
	ADDMODE_ABSY,
	ADDMODE_ABSX,
	ADDMODE_ABS,
};

uint16_t initial_s, initial_p, initial_a, initial_x, initial_y;

void
setup_memory(uint8_t opcode)
{
	bzero(memory, 65536);

	memory[0xFFFC] = SETUP_ADDR & 0xFF;
	memory[0xFFFD] = SETUP_ADDR >> 8;
	uint16_t addr = SETUP_ADDR;
	memory[addr++] = 0xA2; /* LDA #S */
	initial_s = addr;
	memory[addr++] = 0x7F;
	memory[addr++] = 0x9A; /* TXS    */
	memory[addr++] = 0xA9; /* LDA #P */
	initial_p = addr;
	memory[addr++] = 0;
	memory[addr++] = 0x48; /* PHA    */
	memory[addr++] = 0xA9; /* LHA #A */
	initial_a = addr;
	memory[addr++] = 0;
	memory[addr++] = 0xA2; /* LDX #X */
	initial_x = addr;
	memory[addr++] = 0;
	memory[addr++] = 0xA0; /* LDY #Y */
	initial_y = addr;
	memory[addr++] = 0;
	memory[addr++] = 0x28; /* PLP    */
	memory[addr++] = 0x4C; /* JMP    */
	memory[addr++] = INSTRUCTION_ADDR & 0xFF;
	memory[addr++] = INSTRUCTION_ADDR >> 8;

	memory[INSTRUCTION_ADDR + 0] = opcode;
	memory[INSTRUCTION_ADDR + 1] = 0;
	memory[INSTRUCTION_ADDR + 2] = 0;
	memory[INSTRUCTION_ADDR + 3] = 0;

	memory[0xFFFE] = BRK_VECTOR & 0xFF;
	memory[0xFFFF] = BRK_VECTOR >> 8;
	memory[BRK_VECTOR] = 0x00; /* loop there */
}

void *state;

void
resetChip_test()
{
	state = initAndResetChip();
	for (int i = 0; i < 62; i++)
		step(state);

	cycle = -1;
}

int
main()
{
	state = initAndResetChip();

	for (int opcode = 0x00; opcode <= 0xFF; opcode++) {
//	for (int opcode = 0xA9; opcode <= 0xAA; opcode++) {
//	for (int opcode = 0x15; opcode <= 0x15; opcode++) {
		printf("$%02X: ", opcode);

		/**************************************************
		 * find out length of instruction in bytes
		 **************************************************/
		setup_memory(opcode);
		resetChip_test();
		int i;
		for (i = 0; i < MAX_CYCLES; i++) {
			step(state);
			if (IS_READING(BRK_VECTOR))
				break;
		};

		if (i == MAX_CYCLES) {
			data[opcode].crash = YES;
		} else {
			data[opcode].crash = NO;
			uint16_t brk_addr = memory[0x0100+readSP(state)+2] | memory[0x0100+readSP(state)+3]<<8;
			data[opcode].length = brk_addr - INSTRUCTION_ADDR - BRK_LENGTH;

			/**************************************************
			 * find out length of instruction in cycles
			 **************************************************/
			setup_memory(opcode);
			resetChip_test();
			for (i = 0; i < MAX_CYCLES; i++) {
				step(state);
//				chipStatus();
//printf("cycle = %d  %x\n", cycle, readIR());
				if (readIR(state) == 0x00)
					break;
			};
			if (cycle)
				data[opcode].cycles = cycle / 2;
			else
				data[opcode].cycles = 0;

			/**************************************************
			 * find out zp or abs reads
			 **************************************************/
			setup_memory(opcode);
			memory[initial_x] = X_OFFSET;
			memory[initial_y] = Y_OFFSET;
			memory[MAGIC_8 + X_OFFSET + 0] = MAGIC_IZX & 0xFF;
			memory[MAGIC_8 + X_OFFSET + 1] = MAGIC_IZX >> 8;
			memory[MAGIC_8 + 0] = MAGIC_IZY & 0xFF;
			memory[MAGIC_8 + 1] = MAGIC_IZY >> 8;
			resetChip_test();
			if (data[opcode].length == 2) {
				memory[INSTRUCTION_ADDR + 1] = MAGIC_8;
			} else if (data[opcode].length == 3) {
				memory[INSTRUCTION_ADDR + 1] = MAGIC_16 & 0xFF;
				memory[INSTRUCTION_ADDR + 2] = MAGIC_16 >> 8;
			}

			data[opcode].zp = NO;
			data[opcode].abs = NO;
			data[opcode].zpx = NO;
			data[opcode].absx = NO;
			data[opcode].zpy = NO;
			data[opcode].absy = NO;
			data[opcode].izx = NO;
			data[opcode].izy = NO;

			data[opcode].reads = NO;
			data[opcode].writes = NO;

			for (i = 0; i < data[opcode].cycles * 2 + 2; i++) {
				step(state);
				if (IS_READ_CYCLE || IS_WRITE_CYCLE) {
//printf("RW@ %X\n", readAddressBus(state));
					BOOL is_data_access = YES;
					if (readAddressBus(state) == MAGIC_8)
						data[opcode].zp = YES;
					else if (readAddressBus(state) == MAGIC_16)
						data[opcode].abs = YES;
					else if (readAddressBus(state) == MAGIC_8 + X_OFFSET)
						data[opcode].zpx = YES;
					else if (readAddressBus(state) == MAGIC_16 + X_OFFSET)
						data[opcode].absx = YES;
					else if (readAddressBus(state) == MAGIC_8 + Y_OFFSET)
						data[opcode].zpy = YES;
					else if (readAddressBus(state) == MAGIC_16 + Y_OFFSET)
						data[opcode].absy = YES;
					else if (readAddressBus(state) == MAGIC_IZX)
						data[opcode].izx = YES;
					else if (readAddressBus(state) == MAGIC_IZY + Y_OFFSET)
						data[opcode].izy = YES;
					else
						is_data_access = NO;
					if (is_data_access)
						if (IS_READ_CYCLE)
							data[opcode].reads = YES;
						if (IS_WRITE_CYCLE)
							data[opcode].writes = YES;
				}
			};

			data[opcode].addmode = ADDMODE_UNKNOWN;
			if (data[opcode].izy) {
				data[opcode].addmode = ADDMODE_IZY;
			} else if (data[opcode].izx) {
				data[opcode].addmode = ADDMODE_IZX;
			} else if (data[opcode].zpy) {
				data[opcode].addmode = ADDMODE_ZPY;
			} else if (data[opcode].zpx) {
				data[opcode].addmode = ADDMODE_ZPX;
			} else if (data[opcode].zp) {
				data[opcode].addmode = ADDMODE_ZP;
			} else if (data[opcode].absy) {
				data[opcode].addmode = ADDMODE_ABSY;
			} else if (data[opcode].absx) {
				data[opcode].addmode = ADDMODE_ABSX;
			} else if (data[opcode].abs) {
				data[opcode].addmode = ADDMODE_ABS;
			}

			/**************************************************/

			uint8_t magics[] = { 
				/* all 8 bit primes */
				2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
//				31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
//				73, 79, 83, 89, 97, 101, 103, 107, 109, 113,
//				127, 131, 137, 139, 149, 151, 157, 163, 167, 173,
//				179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
				233, 239, 241, 251,
				/* and some other interesting numbers */
				0, 1, 0x55, 0xAA, 0xFF
				};
			/**************************************************
			 * find out inputs
			 **************************************************/
//printf("AAA\n");
			for (int k = 0; k < 5; k++) {
				BOOL different = NO;
				int reads, writes;
				uint16_t read[100], write[100], write_data[100];
				uint8_t end_a, end_x, end_y, end_s, end_p;
				for (int j = 0; j < sizeof(magics)/sizeof(*magics); j++) {
					setup_memory(opcode);
					if (data[opcode].length == 2) {
						memory[INSTRUCTION_ADDR + 1] = MAGIC_8;
					} else if (data[opcode].length == 3) {
						memory[INSTRUCTION_ADDR + 1] = MAGIC_16 & 0xFF;
						memory[INSTRUCTION_ADDR + 2] = MAGIC_16 >> 8;
					}
					switch (k) {
						case 0: memory[initial_a] = magics[j]; break;
						case 1: memory[initial_x] = magics[j]; break;
						case 2: memory[initial_y] = magics[j]; break;
						case 3: memory[initial_s] = magics[j]; break;
						case 4: memory[initial_p] = magics[j]; break;
					}
#define MAGIC_DATA8 3
					switch (data[opcode].addmode) {
						case ADDMODE_IZY:
							//TODO
							break;
						case ADDMODE_IZX:
							//TODO
							break;
						case ADDMODE_ZPY:
							memory[MAGIC_8 + memory[initial_y]] = MAGIC_DATA8;
							break;
						case ADDMODE_ZPX:
							memory[MAGIC_8 + memory[initial_x]] = MAGIC_DATA8;
							break;
						case ADDMODE_ZP:
							memory[MAGIC_8] = MAGIC_DATA8;
							break;
						case ADDMODE_ABSY:
							memory[MAGIC_16 + memory[initial_y]] = MAGIC_DATA8;
							break;
						case ADDMODE_ABSX:
							memory[MAGIC_16 + memory[initial_x]] = MAGIC_DATA8;
							break;
						case ADDMODE_ABS:
							memory[MAGIC_16] = MAGIC_DATA8;
							break;
					}
					resetChip_test();
					writes = 0;
					reads = 0;
					for (i = 0; i < data[opcode].cycles * 2 + 2; i++) {
						step(state);
						if (IS_READ_CYCLE) {
							if (!j)
								read[reads++] = readAddressBus(state);
							else
								if (read[reads++] != readAddressBus(state)) {
									different = YES;
//printf("[[[%d]]]", __LINE__);
									break;
								}
						}
						if (IS_WRITE_CYCLE) {
							if (!j) {
								write[writes] = readAddressBus(state);
								write_data[writes++] = readDataBus(state);
							} else {
								if (write[writes] != readAddressBus(state)) {
									different = YES;
//printf("[[[%d]]]", __LINE__);
									break;
								}
								if (write_data[writes++] != readDataBus(state)) {
//printf("[[[%d:k=%d;%x@%x/%x]]]", __LINE__, k, write[writes-1], write_data[writes-1], readDataBus(state));
									different = YES;
									break;
								}
							}
						}
					};
					if (different) /* bus cycles were different */
						break;
					/* changes A */
					if (!(k == 0)) {
						if (!j) {
							end_a = readA(state);
						} else {
							if (end_a != readA(state)) {
								different = YES;
								break;
							}
						}
					}
					/* changes X */
					if (!(k == 1)) {
						if (!j) {
							end_x = readX(state);
						} else {
							if (end_x != readX(state)) {
								different = YES;
								break;
							}
						}
					}
					/* changes Y */
					if (!(k == 2)) {
						if (!j) {
							end_y = readY(state);
						} else {
							if (end_y != readY(state)) {
								different = YES;
								break;
							}
						}
					}
					/* changes S */
					if (!(k == 3)) {
						if (!j) {
							end_s = readSP(state);
						} else {
							if (end_s != readSP(state)) {
								different = YES;
//printf("[%x/%x]", end_s, readSP(state));
								break;
							}
						}
					}
					/* changes P */
					if (!(k == 4)) {
						if (!j) {
							end_p = readP(state);
						} else {
							if (end_p != readP(state)) {
								different = YES;
								break;
							}
						}
					}
				}
//printf("[%d DIFF: %d]", k, different);
				switch (k) {
					case 0: data[opcode].inputa = different; break;
					case 1: data[opcode].inputx = different; break;
					case 2: data[opcode].inputy = different; break;
					case 3: data[opcode].inputs = different; break;
					case 4: data[opcode].inputp = different; break;
				}
			}
//printf("BBB\n");

			/**************************************************
			 * find out outputs
			 **************************************************/
			data[opcode].outputa = NO;
			data[opcode].outputx = NO;
			data[opcode].outputy = NO;
			data[opcode].outputs = NO;
			data[opcode].outputp = NO;

			for (int j = 0; j < sizeof(magics)/sizeof(*magics) - 5; j++) {
				setup_memory(opcode);
				memory[initial_a] = magics[j + 0];
				memory[initial_x] = magics[j + 1];
				memory[initial_y] = magics[j + 2];
				memory[initial_s] = magics[j + 3];
				memory[initial_p] = magics[j + 4];
				if (data[opcode].length == 2) {
					memory[INSTRUCTION_ADDR + 1] = MAGIC_8;
				} else if (data[opcode].length == 3) {
					memory[INSTRUCTION_ADDR + 1] = MAGIC_16 & 0xFF;
					memory[INSTRUCTION_ADDR + 2] = MAGIC_16 >> 8;
				}
				switch (data[opcode].addmode) {
					case ADDMODE_IZY:
						//TODO
						break;
					case ADDMODE_IZX:
						//TODO
						break;
					case ADDMODE_ZPY:
						memory[MAGIC_8 + memory[initial_y]] = MAGIC_DATA8;
						break;
					case ADDMODE_ZPX:
						memory[MAGIC_8 + memory[initial_x]] = MAGIC_DATA8;
						break;
					case ADDMODE_ZP:
						memory[MAGIC_8] = MAGIC_DATA8;
						break;
					case ADDMODE_ABSY:
						memory[MAGIC_16 + memory[initial_y]] = MAGIC_DATA8;
						break;
					case ADDMODE_ABSX:
						memory[MAGIC_16 + memory[initial_x]] = MAGIC_DATA8;
						break;
					case ADDMODE_ABS:
						memory[MAGIC_16] = MAGIC_DATA8;
						break;
				}
				resetChip_test();
				for (i = 0; i < data[opcode].cycles * 2 + 2; i++) {
					step(state);
				};
				if (readA(state) != magics[j + 0])
					data[opcode].outputa = YES;
				if (readX(state) != magics[j + 1])
					data[opcode].outputx = YES;
				if (readY(state) != magics[j + 2])
					data[opcode].outputy = YES;
				if (readSP(state) != magics[j + 3])
					data[opcode].outputs = YES;
				if ((readP(state) & 0xCF) != (magics[j + 4] & 0xCF)) /* NV#BDIZC */
					data[opcode].outputp = YES;
			}
		}

		if (data[opcode].absx || data[opcode].zpx || data[opcode].izx) {
			if (!data[opcode].inputx)
				printf("input X?? ");
			data[opcode].inputx = NO;
		}
		if (data[opcode].absy || data[opcode].zpy || data[opcode].izy) {
			if (!data[opcode].inputy)
				printf("input Y?? ");
			data[opcode].inputy = NO;
		}

		if (data[opcode].crash) {
			printf("CRASH\n");
		} else {
			printf("bytes: ");
			if (data[opcode].length < 0 || data[opcode].length > 9)
				printf("X ");
			else
				printf("%d ", data[opcode].length);
			printf("cycles: %d ", data[opcode].cycles);
			if (data[opcode].inputa)
				printf("A");
			else
				printf("_");
			if (data[opcode].inputx)
				printf("X");
			else
				printf("_");
			if (data[opcode].inputy)
				printf("Y");
			else
				printf("_");
			if (data[opcode].inputs)
				printf("S");
			else
				printf("_");
			if (data[opcode].inputp)
				printf("P");
			else
				printf("_");

			printf("=>");

			if (data[opcode].outputa)
				printf("A");
			else
				printf("_");
			if (data[opcode].outputx)
				printf("X");
			else
				printf("_");
			if (data[opcode].outputy)
				printf("Y");
			else
				printf("_");
			if (data[opcode].outputs)
				printf("S");
			else
				printf("_");
			if (data[opcode].outputp)
				printf("P");
			else
				printf("_");

			printf(" ");

			if (data[opcode].reads)
				printf("R");
			else
				printf("_");

			if (data[opcode].writes)
				printf("W");
			else
				printf("_");

			printf(" ");

			switch (data[opcode].addmode) {
				case ADDMODE_IZY: printf("izy"); break;
				case ADDMODE_IZX: printf("izx"); break;
				case ADDMODE_ZPY: printf("zpy"); break;
				case ADDMODE_ZPX: printf("zpx"); break;
				case ADDMODE_ZP: printf("zp"); break;
				case ADDMODE_ABSY: printf("absy"); break;
				case ADDMODE_ABSX: printf("absx"); break;
				case ADDMODE_ABS: printf("abs"); break;
			}

			printf("\n");
		}
	}
}
