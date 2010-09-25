/*
 Copyright (c) 2010 Michael Steil, Brian Silverman, Barry Silverman

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/

int verbose = 0;

/************************************************************
 *
 * Libc Functions and Basic Data Types
 *
 ************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef int BOOL;

#define NO 0
#define YES 1

/************************************************************
 *
 * 6502 Description: Nodes, Transistors and Probes
 *
 ************************************************************/

/* nodes */
#include "segdefs.h"
/* transistors */
#include "transdefs.h"
/* node numbers of probes */
#include "nodenames.h"

/* the 6502 consists of this many nodes and transistors */
#define NODES 1725
#define TRANSISTORS 3510

/************************************************************
 *
 * Global Data Types
 *
 ************************************************************/

/* the smallest types to fit the numbers */
typedef uint16_t nodenum_t;
typedef uint16_t transnum_t;
typedef uint16_t count_t;
typedef uint8_t state_t;

/************************************************************
 *
 * Bitmap Data Structures and Algorithms
 *
 ************************************************************/

#if 1
typedef unsigned long long bitmap_t;
#define BITMAP_SHIFT 6
#define BITMAP_MASK 63
#else
typedef unsigned int bitmap_t;
#define BITMAP_SHIFT 5
#define BITMAP_MASK 31
#endif

#define DECLARE_BITMAP(name, count) bitmap_t name[count/sizeof(bitmap_t)+1]

static inline void
bitmap_clear(bitmap_t *bitmap, count_t count)
{
	bzero(bitmap, count/sizeof(bitmap_t)+1);
}

static inline void
set_bitmap(bitmap_t *bitmap, int index, BOOL state)
{
	if (state)
		bitmap[index>>BITMAP_SHIFT] |= 1ULL << (index & BITMAP_MASK);
	else
		bitmap[index>>BITMAP_SHIFT] &= ~(1ULL << (index & BITMAP_MASK));
}

static inline BOOL
get_bitmap(bitmap_t *bitmap, int index)
{
	return (bitmap[index>>BITMAP_SHIFT] >> (index & BITMAP_MASK)) & 1;
}

/************************************************************
 *
 * Data Structures for Nodes
 *
 ************************************************************/

/* everything that describes a node */
DECLARE_BITMAP(nodes_pullup, NODES);
DECLARE_BITMAP(nodes_pulldown, NODES);
DECLARE_BITMAP(nodes_state_value, NODES);
DECLARE_BITMAP(nodes_state_floating, NODES);
nodenum_t nodes_gates[NODES][NODES];
nodenum_t nodes_c1c2s[NODES][2*NODES];
count_t nodes_gatecount[NODES];
count_t nodes_c1c2count[NODES];

static inline void
set_nodes_pullup(transnum_t t, BOOL state)
{
	set_bitmap(nodes_pullup, t, state);
}

static inline BOOL
get_nodes_pullup(transnum_t t)
{
	return get_bitmap(nodes_pullup, t);
}

static inline void
set_nodes_pulldown(transnum_t t, BOOL state)
{
	set_bitmap(nodes_pulldown, t, state);
}

static inline BOOL
get_nodes_pulldown(transnum_t t)
{
	return get_bitmap(nodes_pulldown, t);
}

static inline void
set_nodes_state_value(transnum_t t, BOOL state)
{
	set_bitmap(nodes_state_value, t, state);
}

static inline BOOL
get_nodes_state_value(transnum_t t)
{
	return get_bitmap(nodes_state_value, t);
}

static inline void
set_nodes_state_floating(transnum_t t, BOOL state)
{
	set_bitmap(nodes_state_floating, t, state);
}

static inline BOOL
get_nodes_state_floating(transnum_t t)
{
	return get_bitmap(nodes_state_floating, t);
}

/************************************************************
 *
 * Data Structures and Algorithms for Transistors
 *
 ************************************************************/

/* everything that describes a transistor */
nodenum_t transistors_gate[TRANSISTORS];
nodenum_t transistors_c1[TRANSISTORS];
nodenum_t transistors_c2[TRANSISTORS];
DECLARE_BITMAP(transistors_on, TRANSISTORS);

static inline void
set_transistors_on(transnum_t t, BOOL state)
{
	set_bitmap(transistors_on, t, state);
}

static inline BOOL
get_transistors_on(transnum_t t)
{
	return get_bitmap(transistors_on, t);
}

/************************************************************
 *
 * Data Structures and Algorithms for Lists
 *
 ************************************************************/

/* list of nodes that need to be recalculated */
typedef struct {
	nodenum_t *list;
	count_t count;
	bitmap_t *bitmap;
} list_t;

/* the nodes we are working with */
nodenum_t list1[NODES];
DECLARE_BITMAP(bitmap1, NODES);
list_t listin = {
	.list = list1,
	.bitmap = bitmap1
};

/* the nodes we are collecting for the next run */
nodenum_t list2[NODES];
DECLARE_BITMAP(bitmap2, NODES);
list_t listout = {
	.list = list2,
	.bitmap = bitmap2
};

static inline void
listin_fill(const nodenum_t *source, count_t count)
{
	bcopy(source, listin.list, count * sizeof(nodenum_t));
	listin.count = count;
}

static inline nodenum_t
listin_get(count_t i)
{
	return listin.list[i];
}

static inline count_t
listin_count()
{
	return listin.count;
}

static inline void
lists_switch()
{
	list_t tmp = listin;
	listin = listout;
	listout = tmp;
}

static inline void
listout_clear()
{
	listout.count = 0;
	bitmap_clear(listout.bitmap, NODES);
}

static inline void
listout_add(nodenum_t i)
{
	listout.list[listout.count++] = i;
	set_bitmap(listout.bitmap, i, 1);
}

static inline BOOL
listout_contains(nodenum_t el)
{
	return get_bitmap(listout.bitmap, el);
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
DECLARE_BITMAP(groupbitmap, NODES);

static inline void
group_clear()
{
	groupcount = 0;
	bitmap_clear(groupbitmap, NODES);
}

static inline void
group_add(nodenum_t i)
{
	group[groupcount++] = i;
	set_bitmap(groupbitmap, i, 1);
}

static inline nodenum_t
group_get(count_t n)
{
	return group[n];
}

static inline BOOL
group_contains(nodenum_t el)
{
	return get_bitmap(groupbitmap, el);
}

static inline count_t
group_count()
{
	return groupcount;
}

/************************************************************
 *
 * Node State
 *
 ************************************************************/

void recalcNodeList(const nodenum_t *source, count_t count);

static inline void
setNode(nodenum_t nn, BOOL state)
{
	set_nodes_pullup(nn, state);
	set_nodes_pulldown(nn, !state);
	recalcNodeList(&nn, 1);
}

static inline void
setLow(nodenum_t nn)
{
	setNode(nn, 0);
}

static inline void
setHigh(nodenum_t nn)
{
	setNode(nn, 1);
}

static inline BOOL
isNodeHigh(nodenum_t nn)
{
	return get_nodes_state_value(nn);
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

/*
 * 1. if the group is connected to GND, it's 0
 * 2. if the group is connected to VCC, it's 1
 * 3a. if there is a pullup node, it's 1
 * 3b. if there is a pulldown node, it's 0
 * (if both 1 and 2 are true, the first pullup or pulldown wins, with
 * a statistical advantage towards STATE_1)
 * 4. otherwise, if there is an 1/floating node, it's 1/floating
 * 5. otherwise, it's 0/floating (if there is a 0/floating node, which is always the case)
 */
static inline void
getNodeValue(BOOL *value, BOOL *floating)
{
	if (group_contains(vss)) {
		*value = 0;
		*floating = 0;
		return;
	}

	if (group_contains(vcc)) {
		*value = 1;
		*floating = 0;
		return;
	}

	*value = 0;
	*floating = 1;

	for (count_t i = 0; i < group_count(); i++) {
		nodenum_t nn = group_get(i);
		if (get_nodes_pullup(nn)) {
			*value = 1;
			*floating = 0;
			return;
		}
		if (get_nodes_pulldown(nn)) {
			*value = 0;
			*floating = 0;
			return;
		}
		if (get_nodes_state_value(nn) && get_nodes_state_floating(nn))
			*value = 1;
	}
}

void
addRecalcNode(nodenum_t nn)
{
	/* no need to analyze VCC or GND */
	if (nn == vss || nn == vcc)
		return;

	/* we already know about this node */
	if (listout_contains(nn))
		return;

	/* add node to list */
	listout_add(nn);
}

void
floatnode(nodenum_t nn)
{
	/* VCC and GND are constant */
	if (nn != vss && nn != vcc)
		set_nodes_state_floating(nn, 1);
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

	group_clear();

	/*
	 * get all nodes that are connected through
	 * transistors, starting with this one
	 */
	addNodeToGroup(node);

	/* get the state of the group */
	BOOL newv_value, newv_floating;
	getNodeValue(&newv_value, &newv_floating);
	/*
	 * now all nodes in this group are in this state,
	 * - all transistors switched by nodes the group
	 *   need to be recalculated
	 * - all nodes behind the transistor are collected
	 *   and must be looked at in the next run
	 */
	for (count_t i = 0; i < group_count(); i++) {
		nodenum_t nn = group_get(i);
		set_nodes_state_value(nn, newv_value);
		set_nodes_state_floating(nn, newv_floating);
		for (count_t t = 0; t < nodes_gatecount[nn]; t++)
			recalcTransistor(nodes_gates[nn][t]);
	}
}

/*
 * NOTE: "list" as provided by the caller must
 * at least be able to hold NODES elements!
 */
void
recalcNodeList(const nodenum_t *source, count_t count)
{
	listin_fill(source, count);

	for (int j = 0; j < 100; j++) {	/* loop limiter */
		if (!listin_count())
			return;

		listout_clear();

		/*
		 * for all nodes, follow their paths through
		 * turned-on transistors, find the state of the
		 * path and assign it to all nodes, and re-evaluate
		 * all transistors controlled by this path, collecting
		 * all nodes that changed because of it for the next run
		 */
		for (count_t i = 0; i < listin_count(); i++)
			recalcNode(listin_get(i));

		/*
		 * make the secondary list our primary list, use
		 * the data storage of the primary list as the
		 * secondary list
		 */
		lists_switch();
	}
}

void
recalcAllNodes()
{
	nodenum_t temp[NODES];
	for (count_t i = 0; i < NODES; i++)
		temp[i] = i;
	recalcNodeList(temp, NODES);
}

/************************************************************
 *
 * Address Bus and Data Bus Interface
 *
 ************************************************************/

uint8_t memory[65536]; /* XXX must be hooked up with RAM[] in runtime.c */

/* the nodes that make the data bus */
const nodenum_t dbnodes[8] = { db0, db1, db2, db3, db4, db5, db6, db7 };

void
writeDataBus(uint8_t d)
{
	for (int i = 0; i < 8; i++) {
		nodenum_t nn = dbnodes[i];
		setNode(nn, d & 1);
		d >>= 1;
	}

	/* recalc all nodes connected starting from the data bus */
	recalcNodeList(dbnodes, 8);
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

static int cycle;

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
		P &= 0x7C; /* clear N, Z, C */
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
		set_nodes_pullup(i, segdefs[i] == 1);
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
	set_nodes_state_value(vss, 0);
	set_nodes_state_floating(vss, 0);
	set_nodes_state_value(vcc, 1);
	set_nodes_state_floating(vcc, 0);
}

void
initChip()
{
	/* all nodes are floating */
	for (nodenum_t nn = 0; nn < NODES; nn++) {
		set_nodes_state_value(nn, 0);
		set_nodes_state_floating(nn, 1);
	}
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
