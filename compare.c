
void
full_step(uint16_t *a, uint8_t *d, BOOL *r_w)
{
	step();
	step();

	*a = readAddressBus();
	*d = readDataBus();
	*r_w = isNodeHigh(rw);
}

#define RESET 0xF000
#define A_OUT 0xF100
#define X_OUT 0xF101
#define Y_OUT 0xF102
#define S_OUT 0xF103
#define P_OUT 0xF104

#define TRIGGER1 0x5555
uint16_t trigger2;
#define TRIGGER3 0xAAAA

void
setup_memory(int length, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t A, uint8_t X, uint8_t Y, uint8_t S, uint8_t P)
{
	bzero(memory, 65536);

	memory[0xFFFC] = RESET & 0xFF;
	memory[0xFFFD] = RESET >> 8;
	memory[RESET + 0x00] = 0xA2; /* LDA #S */
	memory[RESET + 0x01] = S;
	memory[RESET + 0x02] = 0x9A; /* TXS    */
	memory[RESET + 0x03] = 0xA9; /* LDA #P */
	memory[RESET + 0x04] = P;
	memory[RESET + 0x05] = 0x48; /* PHA    */
	memory[RESET + 0x06] = 0xA9; /* LHA #A */
	memory[RESET + 0x07] = A;
	memory[RESET + 0x08] = 0xA2; /* LDX #X */
	memory[RESET + 0x09] = X;
	memory[RESET + 0x0A] = 0xA0; /* LDY #Y */
	memory[RESET + 0x0B] = Y;
	memory[RESET + 0x0C] = 0x28; /* PLP    */
	memory[RESET + 0x0D] = 0x8D; /* STA TRIGGER1 */
	memory[RESET + 0x0E] = TRIGGER1 & 0xFF;
	memory[RESET + 0x0F] = TRIGGER1 >> 8;
	memory[RESET + 0x10] = b1;
	uint16_t addr = RESET + 0x11;
	if (length >= 2)
		memory[addr++] = b2;
	if (length >= 3)
		memory[addr++] = b3;
	trigger2 = addr;
	memory[addr++] = 0x08; /* PHP */
	memory[addr++] = 0x8D; /* STA A_OUT */
	memory[addr++] = A_OUT & 0xFF;
	memory[addr++] = A_OUT >> 8;
	memory[addr++] = 0x8E; /* STX X_OUT */
	memory[addr++] = X_OUT & 0xFF;
	memory[addr++] = X_OUT >> 8;
	memory[addr++] = 0x8C; /* STY Y_OUT */
	memory[addr++] = Y_OUT & 0xFF;
	memory[addr++] = Y_OUT >> 8;
	memory[addr++] = 0x68; /* PLA */
	memory[addr++] = 0x8D; /* STA P_OUT */
	memory[addr++] = P_OUT & 0xFF;
	memory[addr++] = P_OUT >> 8;
	memory[addr++] = 0xBA; /* TSX */
	memory[addr++] = 0x8E; /* STX S_OUT */
	memory[addr++] = S_OUT & 0xFF;
	memory[addr++] = S_OUT >> 8;
	memory[addr++] = 0x8D; /* STA TRIGGER3 */
	memory[addr++] = TRIGGER3 & 0xFF;
	memory[addr++] = TRIGGER3 >> 8;
	memory[addr++] = 0xA9; /* LDA #$00  */
	memory[addr++] = 0x00;
	memory[addr++] = 0xF0; /* BEQ .     */
	memory[addr++] = 0xFE;
}

#define IS_READ_CYCLE (isNodeHigh(clk0) && isNodeHigh(rw))
#define IS_WRITE_CYCLE (isNodeHigh(clk0) && !isNodeHigh(rw))
#define IS_READING(a) (IS_READ_CYCLE && readAddressBus() == (a))

#define MAX_CYCLES 100

enum {
	STATE_BEFORE_INSTRUCTION,
	STATE_DURING_INSTRUCTION,
	STATE_FIRST_FETCH
};

void
setup_perfect()
{
	setupNodesAndTransistors();
	verbose = 0;
}

uint16_t instr_ab[10];
uint8_t instr_db[10];
BOOL instr_rw[10];

int
perfect_measure_instruction()
{
	int state = STATE_BEFORE_INSTRUCTION;
	int c = 0;
	for (int i = 0; i < MAX_CYCLES; i++) {
		uint16_t ab;
		uint8_t db;
		BOOL r_w;
		full_step(&ab, &db, &r_w);

		if (state == STATE_DURING_INSTRUCTION && ab > trigger2) {
			/*
			 * we see the FIRST fetch of the next instruction,
			 * the test instruction MIGHT be done
			 */
			state = STATE_FIRST_FETCH;
		} 

		if (state == STATE_DURING_INSTRUCTION) {
			instr_rw[c] = r_w;
			instr_ab[c] = ab;
			instr_db[c] = db;
			c++;
		}

		if (ab == TRIGGER1) {
			state = STATE_DURING_INSTRUCTION; /* we're done writing the trigger value; now comes the instruction! */
		} 
		if (ab == TRIGGER3) {
			break; /* we're done dumping the CPU state */
		}
	};

	return c;
}

extern void setup_emu(void);
void reset_emu(void);
extern int emu_measure_instruction(void);

int
main()
{
	setup_perfect();
//	setup_memory(1, 0xEA, 0x00, 0x00, 0, 0, 0, 0, 0);
//	setup_memory(2, 0xA9, 0x00, 0x00, 0, 0, 0, 0, 0);
//	setup_memory(2, 0xAD, 0x00, 0x10, 0, 0, 0, 0, 0);
//	setup_memory(3, 0xFE, 0x00, 0x10, 0, 0, 0, 0, 0);
//	setup_memory(3, 0x9D, 0xFF, 0x10, 0, 2, 0, 0, 0);
	setup_memory(1, 0x28, 0x00, 0x00, 0x55, 0, 0, 0x80, 0);
	resetChip();
	int instr_cycles = perfect_measure_instruction();

	for (int c = 0; c < instr_cycles; c++ ) {
		printf("T%d ", c+1);
		if (instr_rw[c])
			printf("R $%04X\n", instr_ab[c]);
		else
			printf("W $%04X = $%02X\n", instr_ab[c], instr_db[c]);
	}

	setup_emu();
	setup_memory(1, 0x48, 0x00, 0x00, 0x55, 0, 0, 0x80, 0);
	reset_emu();
	int instr_cycles2 = emu_measure_instruction();

}
