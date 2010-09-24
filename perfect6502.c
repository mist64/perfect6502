int verbose = 0;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef int BOOL;

#define NO 0
#define YES 1

/* nodes */
#include "segdefs.h"
/* transistors */
#include "transdefs.h"
/* node numbers of probes */
#include "nodenames.h"

enum {
	STATE_VCC,
	STATE_PU,
	STATE_FH,
#define isNodeHigh(nn) (nodes_state[nn] <= STATE_FH) /* everything above is high */
	STATE_GND,
	STATE_FL,
	STATE_PD,
};

/* the 6502 consists of this many nodes and transistors */
#define NODES 1725
#define TRANSISTORS 3510

/* the smallest types to fit the numbers */
typedef uint16_t nodenum_t;
typedef uint16_t transnum_t;
typedef uint16_t count_t;
typedef uint8_t state_t;

/* everything that describes a node */
BOOL nodes_pullup[NODES];
BOOL nodes_pulldown[NODES];
state_t nodes_state[NODES];
nodenum_t nodes_gates[NODES][NODES];
nodenum_t nodes_c1c2s[NODES][2*NODES];
count_t nodes_gatecount[NODES];
count_t nodes_c1c2count[NODES];

/* everything that describes a transistor */
nodenum_t transistors_gate[TRANSISTORS];
nodenum_t transistors_c1[TRANSISTORS];
nodenum_t transistors_c2[TRANSISTORS];

int transistors_on[TRANSISTORS/sizeof(int)+1]; /* bitfield */

int cycle;

uint8_t memory[65536]; /* XXX must be hooked up with RAM[] in runtime.c */

/* list of nodes that need to be recalculated */
typedef struct {
	nodenum_t *list;
	count_t count;
	int *bitmap;
} list_t;

list_t recalc;

/************************************************************
 *
 * Helpers for Data Structures
 *
 ************************************************************/

void
set_transistors_on(transnum_t t, BOOL state)
{
	if (state)
		transistors_on[t>>5] |= 1 << (t & 31);
	else
		transistors_on[t>>5] &= ~(1 << (t & 31));
}

BOOL
get_transistors_on(transnum_t t)
{
	return (transistors_on[t>>5] >> (t & 31)) & 1;
}

BOOL
recalcListContains(nodenum_t el)
{
	return (recalc.bitmap[el>>5] >> (el & 31)) & 1;
}

/************************************************************
 *
 * Data Structures and Algorithms for Groups of Nodes
 *
 ************************************************************/

/*
 * a group is a set of connected nodes
 * that consequently share the same potential
 *
 * we use an array and a count for O(1) insert and
 * iteration, and a redundant bitmap for O(1) lookup
 */
static nodenum_t group[NODES];
static count_t groupcount;
static int groupbitmap[NODES/sizeof(int)+1];

static inline void
group_init()
{
	groupcount = 0;
	bzero(groupbitmap, sizeof(groupbitmap));
}

static inline void
group_add(nodenum_t i)
{
	group[groupcount++] = i;
	groupbitmap[i>>5] |= 1 << (i & 31);
}

static inline nodenum_t
group_get(count_t n)
{
	return group[n];
}

static inline BOOL
group_contains(nodenum_t el)
{
	return (groupbitmap[el>>5] >> (el & 31)) & 1;
}

static inline count_t
group_count()
{
	return groupcount;
}

/************************************************************
 *
 * Node and Transistor Emulation
 *
 ************************************************************/

void addNodeToGroup(nodenum_t i); /* recursion! */

void
addNodeTransistor(nodenum_t node, transnum_t t)
{
	/* if the transistor does not connect c1 and c2, we stop here */
	if (!get_transistors_on(t))
		return;

	/* if original node was connected to c1, put c2 into list and vice versa */
	if (transistors_c1[t] == node)
		addNodeToGroup(transistors_c2[t]);
	else
		addNodeToGroup(transistors_c1[t]);
}

void
addNodeToGroup(nodenum_t i)
{
	if (group_contains(i))
		return;

	group_add(i);

	if (i == vss || i == vcc)
		return;

	for (count_t t = 0; t < nodes_c1c2count[i]; t++)
		addNodeTransistor(i, nodes_c1c2s[i][t]);
}

// 1. if there is a pullup node, it's STATE_PU
// 2. if there is a pulldown node, it's STATE_PD
// (if both 1 and 2 are true, the first pullup or pulldown wins, with
// a statistical advantage towards STATE_PU)
// 3. otherwise, if there is an FH node, it's STATE_FH
// 4. otherwise, it's STATE_FL (if there is an FL node, which is always the case)
state_t
getNodeValue()
{
	if (group_contains(vss))
		return STATE_GND;

	if (group_contains(vcc))
		return STATE_VCC;

	state_t flstate = STATE_FL;

	for (count_t i = 0; i < group_count(); i++) {
		nodenum_t nn = group_get(i);
		if (nodes_pullup[nn])
			return STATE_PU;
		if (nodes_pulldown[nn])
			return STATE_PD;
		if (nodes_state[nn] == STATE_FH)
			flstate = STATE_FH;
	}
	return flstate;
}

void
addRecalcNode(nodenum_t nn)
{
	/* no need to analyze VCC or GND */
	if (nn == vss || nn == vcc)
		return;

	/* we already know about this node */
	if (recalcListContains(nn))
		return;

	/* add node to list */
	recalc.list[recalc.count++] = nn;
	recalc.bitmap[nn>>5] |= 1 << (nn & 31);
}

void
floatnode(nodenum_t nn)
{
	/* VCC and GND are constant */
	if (nn == vss || nn == vcc)
		return;

	state_t state = nodes_state[nn];

	if (state == STATE_GND || state == STATE_PD)
		nodes_state[nn] = STATE_FL;

	if (state == STATE_VCC || state == STATE_PU)
		nodes_state[nn] = STATE_FH;
}

void
recalcTransistor(transnum_t tn)
{
	/* if the gate is high, the transistor should be on */
	BOOL on = isNodeHigh(transistors_gate[tn]);

	/* no change? nothing to do! */
	if (on == get_transistors_on(tn))
		return;

	set_transistors_on(tn, on);

	/* if the transistor is off, both nodes are floating */
	if (!on) {
		floatnode(transistors_c1[tn]);
		floatnode(transistors_c2[tn]);
	}

	/* next time, we'll have to look at both nodes behind the transistor */
	addRecalcNode(transistors_c1[tn]);
	addRecalcNode(transistors_c2[tn]);
}

void
recalcNode(nodenum_t node)
{
	if (node == vss || node == vcc)
		return;

	group_init();

	/*
	 * get all nodes that are connected through
	 * transistors, starting with this one
	 */
	addNodeToGroup(node);

	/* get the state of the group */
	state_t newv = getNodeValue();

	/*
	 * now all nodes in this group are in this state,
	 * - all transistors switched by nodes the group
	 *   need to be recalculated
	 * - all nodes behind the transistor are collected
	 *   and must be looked at in the next run
	 */
	for (count_t i = 0; i < group_count(); i++) {
		nodenum_t nn = group_get(i);
		nodes_state[nn] = newv;
		for (count_t t = 0; t < nodes_gatecount[nn]; t++)
			recalcTransistor(nodes_gates[nn][t]);
	}
}

/*
 * NOTE: "list" as provided by the caller must
 * at least be able to hold NODES elements!
 */
void
recalcNodeList(nodenum_t *list, count_t count)
{
	/* storage for secondary list and two sets of bitmaps */
	nodenum_t list1[NODES];
	int bitmap1[NODES/sizeof(int)+1];
	int bitmap2[NODES/sizeof(int)+1];

	/* the nodes we are working with */
	list_t current;
	current.list = list;
	current.count = count;
	current.bitmap = bitmap2;

	/* the nodes we are collecting for the next run */
	recalc.list = list1;
	recalc.bitmap = bitmap1;

	for (int j = 0; j < 100; j++) {	// loop limiter
		if (!current.count)
			return;

		/* clear secondary list */
		bzero(recalc.bitmap, sizeof(*recalc.bitmap)*NODES/sizeof(int));
		recalc.count = 0;

		/*
		 * for all nodes, follow their paths through
		 * turned-on transistors, find the state of the
		 * path and assign it to all nodes, and re-evaluate
		 * all transistors controlled by this path, collecting
		 * all nodes that changed because of it for the next run
		 */
		for (count_t i = 0; i < current.count; i++)
			recalcNode(current.list[i]);

		/*
		 * make the secondary list our primary list, use
		 * the data storage of the primary list as the
		 * secondary list
		 */
		list_t tmp = current;
		current = recalc;
		recalc = tmp;
	}
}

void
recalcAllNodes()
{
	nodenum_t list[NODES];
	for (count_t i = 0; i < NODES; i++)
		list[i] = i;
	recalcNodeList(list, NODES);
}

static inline void
setNode(nodenum_t nn, BOOL state)
{
	nodes_pullup[nn] = state;
	nodes_pulldown[nn] = !state;
	nodenum_t list[NODES];
	list[0] = nn;
	recalcNodeList(list, 1);
}

void
setLow(nodenum_t nn)
{
	setNode(nn, 0);
}

void
setHigh(nodenum_t nn)
{
	setNode(nn, 1);
}

/************************************************************
 *
 * Address Bus and Data Bus Interface
 *
 ************************************************************/

/* the nodes that make the data bus */
const nodenum_t dbnodes[8] = { db0, db1, db2, db3, db4, db5, db6, db7 };

void
writeDataBus(uint8_t x)
{
	for (int i = 0; i < 8; i++) {
		nodenum_t nn = dbnodes[i];
		nodes_pulldown[nn] = !(x & 1);
		nodes_pullup[nn] = x & 1;
		x >>= 1;
	}

	/* recalc all nodes connected starting from the data bus */
	nodenum_t list[NODES];
	bcopy(dbnodes, list, sizeof(dbnodes));
	recalcNodeList(list, 8);
}

uint8_t mRead(uint16_t a)
{
	if (verbose)
		printf("PEEK($%04X) = $%02X\n", a, memory[a]);
	return memory[a];
}

uint16_t
readAddressBus()
{
	return (isNodeHigh(ab0) << 0) | 
           (isNodeHigh(ab1) << 1) | 
           (isNodeHigh(ab2) << 2) | 
           (isNodeHigh(ab3) << 3) | 
           (isNodeHigh(ab4) << 4) | 
           (isNodeHigh(ab5) << 5) | 
           (isNodeHigh(ab6) << 6) | 
           (isNodeHigh(ab7) << 7) | 
           (isNodeHigh(ab8) << 8) | 
           (isNodeHigh(ab9) << 9) | 
           (isNodeHigh(ab10) << 10) | 
           (isNodeHigh(ab11) << 11) | 
           (isNodeHigh(ab12) << 12) | 
           (isNodeHigh(ab13) << 13) | 
           (isNodeHigh(ab14) << 14) | 
           (isNodeHigh(ab15) << 15); 
}

uint8_t
readDataBus()
{
	return (isNodeHigh(db0) << 0) | 
           (isNodeHigh(db1) << 1) | 
           (isNodeHigh(db2) << 2) | 
           (isNodeHigh(db3) << 3) | 
           (isNodeHigh(db4) << 4) | 
           (isNodeHigh(db5) << 5) | 
           (isNodeHigh(db6) << 6) | 
           (isNodeHigh(db7) << 7);
}

void
mWrite(uint16_t a, uint8_t d)
{
	if (verbose)
		printf("POKE $%04X, $%02X\n", a, d);
	memory[a] = d;
}

/************************************************************
 *
 * Tracing/Debugging
 *
 ************************************************************/
 
uint8_t
readA()
{
	return (isNodeHigh(a0) << 0) | 
           (isNodeHigh(a1) << 1) | 
           (isNodeHigh(a2) << 2) | 
           (isNodeHigh(a3) << 3) | 
           (isNodeHigh(a4) << 4) | 
           (isNodeHigh(a5) << 5) | 
           (isNodeHigh(a6) << 6) | 
           (isNodeHigh(a7) << 7);
}

uint8_t
readX()
{
	return (isNodeHigh(x0) << 0) | 
           (isNodeHigh(x1) << 1) | 
           (isNodeHigh(x2) << 2) | 
           (isNodeHigh(x3) << 3) | 
           (isNodeHigh(x4) << 4) | 
           (isNodeHigh(x5) << 5) | 
           (isNodeHigh(x6) << 6) | 
           (isNodeHigh(x7) << 7);
}

uint8_t
readY()
{
	return (isNodeHigh(y0) << 0) | 
           (isNodeHigh(y1) << 1) | 
           (isNodeHigh(y2) << 2) | 
           (isNodeHigh(y3) << 3) | 
           (isNodeHigh(y4) << 4) | 
           (isNodeHigh(y5) << 5) | 
           (isNodeHigh(y6) << 6) | 
           (isNodeHigh(y7) << 7);
}

uint8_t
readP()
{
	return (isNodeHigh(p0) << 0) | 
           (isNodeHigh(p1) << 1) | 
           (isNodeHigh(p2) << 2) | 
           (isNodeHigh(p3) << 3) | 
           (isNodeHigh(p4) << 4) | 
           (isNodeHigh(p5) << 5) | 
           (isNodeHigh(p6) << 6) | 
           (isNodeHigh(p7) << 7);
}

uint8_t
readNOTIR()
{
	return (isNodeHigh(notir0) << 0) | 
           (isNodeHigh(notir1) << 1) | 
           (isNodeHigh(notir2) << 2) | 
           (isNodeHigh(notir3) << 3) | 
           (isNodeHigh(notir4) << 4) | 
           (isNodeHigh(notir5) << 5) | 
           (isNodeHigh(notir6) << 6) | 
           (isNodeHigh(notir7) << 7);
}

uint8_t
readSP()
{
	return (isNodeHigh(s0) << 0) | 
           (isNodeHigh(s1) << 1) | 
           (isNodeHigh(s2) << 2) | 
           (isNodeHigh(s3) << 3) | 
           (isNodeHigh(s4) << 4) | 
           (isNodeHigh(s5) << 5) | 
           (isNodeHigh(s6) << 6) | 
           (isNodeHigh(s7) << 7);
}

uint8_t
readPCL()
{
	return (isNodeHigh(pcl0) << 0) | 
           (isNodeHigh(pcl1) << 1) | 
           (isNodeHigh(pcl2) << 2) | 
           (isNodeHigh(pcl3) << 3) | 
           (isNodeHigh(pcl4) << 4) | 
           (isNodeHigh(pcl5) << 5) | 
           (isNodeHigh(pcl6) << 6) | 
           (isNodeHigh(pcl7) << 7);
}

uint8_t
readPCH()
{
	return (isNodeHigh(pch0) << 0) | 
           (isNodeHigh(pch1) << 1) | 
           (isNodeHigh(pch2) << 2) | 
           (isNodeHigh(pch3) << 3) | 
           (isNodeHigh(pch4) << 4) | 
           (isNodeHigh(pch5) << 5) | 
           (isNodeHigh(pch6) << 6) | 
           (isNodeHigh(pch7) << 7);
}

uint16_t
readPC()
{
	return (readPCH() << 8) | readPCL();
}

void
chipStatus()
{
	printf("halfcyc:%d phi0:%d AB:%04X D:%02X RnW:%d PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X IR:%02X\n",
			cycle,
			isNodeHigh(clk0),
			readAddressBus(),
	        readDataBus(),
	        isNodeHigh(rw),
			readPC(),
			readA(),
			readX(),
			readY(),
			readSP(),
			readP(),
			readNOTIR() ^ 0xFF);
}

/************************************************************
 *
 * Interface to OS Library Code / Monitor
 *
 ************************************************************/

extern int kernal_dispatch();

/* imported by runtime.c */
uint8_t A, X, Y, S, P;
uint16_t PC;
BOOL N, Z, C;

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
	for (uint16_t addr = 0xFF90; addr < 0xFFF3; addr += 3) {
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

	if (PC >= 0xFF90 && ((PC - 0xFF90) % 3 == 0) && isNodeHigh(clk0)) {
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
		P &= 0x7C; // clear N, Z, C
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

/************************************************************
 *
 * Main Clock Loop
 *
 ************************************************************/

void
halfStep()
{
	BOOL clk = isNodeHigh(clk0);

	/* invert clock */
	setNode(clk0, !clk);

	/* handle memory reads and writes */
	if (clk && isNodeHigh(rw))
		writeDataBus(mRead(readAddressBus()));
	if (!clk && !isNodeHigh(rw))
		mWrite(readAddressBus(), readDataBus());
}

void
step()
{
	halfStep();
	cycle++;
	if (verbose)
		chipStatus();

#if 0
	for (int i = 0; i < NODES; i++) {
//		if (nodes_pullup[i] && nodes_pulldown[i])
//			printf("BOTH %d\n", i);
		if (!nodes_pullup[i] && !nodes_pulldown[i])
			printf("%d ", i);
	}
	printf("\n");
#endif

	handle_monitor();
}

/************************************************************
 *
 * Initialization
 *
 ************************************************************/

void
setupNodesAndTransistors()
{
	count_t i;
	for (i = 0; i < sizeof(segdefs)/sizeof(*segdefs); i++) {
		nodes_pullup[i] = segdefs[i] == 1;
//		nodes_pulldown[i] = !segdefs[i];
		nodes_gatecount[i] = 0;
		nodes_c1c2count[i] = 0;
	}
	for (i = 0; i < sizeof(transdefs)/sizeof(*transdefs); i++) {
		nodenum_t gate = transdefs[i].gate;
		nodenum_t c1 = transdefs[i].c1;
		nodenum_t c2 = transdefs[i].c2;
		transistors_gate[i] = gate;
		transistors_c1[i] = c1;
		transistors_c2[i] = c2;
		nodes_gates[gate][nodes_gatecount[gate]++] = i;
		nodes_c1c2s[c1][nodes_c1c2count[c1]++] = i;
		nodes_c1c2s[c2][nodes_c1c2count[c2]++] = i;
	}
	nodes_state[vss] = STATE_GND;
	nodes_state[vcc] = STATE_VCC;
}

void
initChip()
{
	/* all nodes are floating */
	for (nodenum_t nn = 0; nn < NODES; nn++)
		nodes_state[nn] = STATE_FL;
	/* all transistors are off */
	for (transnum_t tn = 0; tn < TRANSISTORS; tn++) 
		set_transistors_on(tn, NO);

	cycle = 0;

	setLow(res);
	setLow(clk0);
	setHigh(rdy);
	setLow(so);
	setHigh(irq);
	setHigh(nmi);

	recalcAllNodes(); 

	/* hold RESET for 8 cycles */
	for (int i = 0; i < 16; i++)
		step();

	/* release RESET */
	setHigh(res);
}

/************************************************************
 *
 * Main
 *
 ************************************************************/

int
main()
{
	/* set up data structures for efficient emulation */
	setupNodesAndTransistors();
	/* set initial state of nodes, transistors, inputs; RESET chip */
	initChip();
	/* set up memory for user program */
	init_monitor();

	/* emulate the 6502! */
	for (;;)
		step();
}
