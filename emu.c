#include <stdio.h>
#include <stdlib.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

typedef uint8_t BOOL;
#define YES 1
#define NO 0

typedef uint8_t step_t;

static step_t t; /* step inside the instruction */
static uint8_t ir; /* instruction register */
static uint16_t PC;
static uint8_t A, X, Y, S, P;
static uint8_t temp_lo, temp_hi;

#define TEMP16 (temp_lo | temp_hi << 8)

static uint16_t AB;
static uint8_t DB;
static BOOL RW;
extern uint8_t memory[65536];

#define RW_WRITE 0
#define RW_READ 1

uint8_t
LOAD(uint16_t a)
{
	return memory[a];
}

#define T1 (1<<0)
#define T2 (1<<1)
#define T3 (1<<2)
#define T4 (1<<3)
#define T5 (1<<4)
#define T6 (1<<5)
#define T7 (1<<6)

#define IS_T1 (t & T1)
#define IS_T2 (t & T2)
#define IS_T3 (t & T3)
#define IS_T4 (t & T4)
#define IS_T5 (t & T5)
#define IS_T6 (t & T6)
#define IS_T7 (t & T7)

void
IFETCH()
{
	AB = PC;
	RW = RW_READ;
	t = T1;
}

void
init()
{
	t = T1;
	ir = 0;
	PC = 0;
	A = 0;
	X = 0;
	Y = 0;
	S = 0;
	P = 0;
	IFETCH();
}

void
EOI_INCPC_READPC()
{
	PC++;
	t <<= 1;
	AB = PC;
	RW = RW_READ;
}

void
DB_TO_ADDRLO()
{
	temp_lo = DB;
}
void
DB_TO_ADDRHI()
{
	temp_hi = DB;
}

void
EOI()
{
	t <<= 1;
}

void
EOI_INCPC()
{
	PC++;
	EOI();
}

void
EOI_INCPC_READADDR()
{
	EOI_INCPC();
	AB = TEMP16;
	RW = RW_READ;
}

void
EOI_INCPC_WRITEADDR()
{
	EOI_INCPC();
	AB = TEMP16;
	RW = RW_WRITE;
}

void
pha()
{
	printf("%s",__func__);
	if (IS_T2) {
		S--;
		EOI();
	} else if (IS_T3) {
		AB = 0x0100 + S;
		DB = A;
		RW = RW_WRITE;
		S++;
		EOI();
	} else if (IS_T4) {
		IFETCH();
	}
}

void
plp()
{
	printf("%s",__func__);
	if (IS_T2) {
		EOI();
	} else if (IS_T3) {
		temp_lo = S;
		temp_hi = 0x01;
		AB = TEMP16;
		RW = RW_READ;
		S++;
		EOI();
	} else if (IS_T4) {
		temp_lo = S;
		AB = TEMP16;
		RW = RW_READ;
		EOI();
	} else if (IS_T5) {
		P = DB;
		IFETCH();
	}
}

void
txs()
{
	printf("%s",__func__);
	/* T2 */
	S = X;
	IFETCH();
}

void
lda_imm()
{
	printf("%s",__func__);
	/* T2 */
	A = DB;
	PC++;
	IFETCH();
}

void
ldx_imm()
{
	printf("%s",__func__);
	/* T2 */
	X = DB;
	PC++;
	IFETCH();
}

void
ldy_imm()
{
	printf("%s",__func__);
	/* T2 */
	Y = DB;
	PC++;
	IFETCH();
}

void
lda_abs()
{
	printf("%s",__func__);
	if (IS_T2) {
		DB_TO_ADDRLO();
		EOI_INCPC_READPC();
	} else if (IS_T3) {
		DB_TO_ADDRHI();
		EOI_INCPC_READADDR();
	} else if (IS_T4) {
		A = DB;
		IFETCH();
	}
}

void
sta_abs()
{
	printf("%s",__func__);
	if (IS_T2) {
		DB_TO_ADDRLO();
		EOI_INCPC_READPC();
	} else if (IS_T3) {
		DB_TO_ADDRHI();
		DB = A;
		EOI_INCPC_WRITEADDR();
	} else if (IS_T4) {
		IFETCH();
	}
}

static int cycle = 0;

void
emulate_step()
{
	/* memory */
	if (RW == RW_READ) {
		printf("PEEK(%04X)=%02X ", AB, memory[AB]);
		DB = memory[AB];
	} else {
		printf("POKE %04X, %02X ", AB, DB);
		memory[AB] = DB;
	}

	//printf("T%d PC=%04X ", t, PC);
	if (IS_T1) { /* T0: get result of IFETCH */
		printf("fetch");
		ir = DB;
		EOI_INCPC_READPC();
	} else {
		//printf ("IR: %02X ", ir);
		switch (ir) {
			case 0x28: plp();     break;
			case 0x48: pha();     break;
			case 0x8D: sta_abs(); break;
			case 0x9A: txs();     break;
			case 0xA0: ldy_imm(); break;
			case 0xA2: ldx_imm(); break;
			case 0xA9: lda_imm(); break;
			case 0xAD: lda_abs(); break;
			default:
				printf("unimplemented opcode: %02X\n", ir);
				exit(0);
		}
	}

	printf("\ncycle:%d phi0:1 AB:%04X D:%02X RnW:%d PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X IR:%02X",
			cycle,
			AB,
	        DB,
	        RW,
			PC,
			A,
			X,
			Y,
			S,
			P,
			ir);

}

void
setup_emu()
{
	init();
}

void
reset_emu()
{
	init();
	PC = memory[0xFFFC] | memory[0xFFFD] << 8;
	printf("PC %x\n", PC);
	IFETCH();
}

int
emu_measure_instruction()
{

	for (;;) {
		printf("cycle %d: ", cycle);
		emulate_step();
		printf("\n");
		cycle++;
	}
	return 0;
}
