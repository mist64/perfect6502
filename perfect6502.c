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

//#define DEBUG

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
typedef unsigned int BOOL;

#define YES 1
#define NO 0

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
#define NODES (sizeof(segdefs)/sizeof(*segdefs))
#define TRANSISTORS (sizeof(transdefs)/sizeof(*transdefs))

/************************************************************
 *
 * Global Data Types
 *
 ************************************************************/

/* the smallest types to fit the numbers */
typedef uint16_t nodenum_t;
typedef uint16_t transnum_t;
typedef uint16_t count_t;

/************************************************************
 *
 * Bitmap Data Structures and Algorithms
 *
 ************************************************************/

#if 0 /* on 64 bit CPUs */
typedef unsigned long long bitmap_t;
#define BITMAP_SHIFT 6
#define BITMAP_MASK 63
#define ONE 1ULL
#else
typedef unsigned int bitmap_t;
#define BITMAP_SHIFT 5
#define BITMAP_MASK 31
#define ONE 1
#endif

#define WORDS_FOR_BITS(a) (a/(sizeof(bitmap_t) * 8)+1)
#define DECLARE_BITMAP(name, count) bitmap_t name[WORDS_FOR_BITS(count)]

static inline void
bitmap_clear(bitmap_t *bitmap, count_t count)
{
	bzero(bitmap, WORDS_FOR_BITS(count)*sizeof(bitmap_t));
}

static inline void
set_bitmap(bitmap_t *bitmap, int index, BOOL state)
{
	if (state)
		bitmap[index>>BITMAP_SHIFT] |= ONE << (index & BITMAP_MASK);
	else
		bitmap[index>>BITMAP_SHIFT] &= ~(ONE << (index & BITMAP_MASK));
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

/* list of nodes that need to be recalculated */
typedef struct {
	nodenum_t *list;
	count_t count;
} list_t;

typedef struct {
	/* everything that describes a node */
	DECLARE_BITMAP(nodes_pullup, NODES);
	DECLARE_BITMAP(nodes_pulldown, NODES);
	DECLARE_BITMAP(nodes_value, NODES);
	nodenum_t nodes_gates[NODES][NODES];
	nodenum_t nodes_c1c2s[NODES][2*NODES];
	count_t nodes_gatecount[NODES];
	count_t nodes_c1c2count[NODES];
	nodenum_t nodes_dependants[NODES];
	nodenum_t nodes_left_dependants[NODES];
	nodenum_t nodes_dependant[NODES][NODES];
	nodenum_t nodes_left_dependant[NODES][NODES];

	/* everything that describes a transistor */
	nodenum_t transistors_gate[TRANSISTORS];
	nodenum_t transistors_c1[TRANSISTORS];
	nodenum_t transistors_c2[TRANSISTORS];
	DECLARE_BITMAP(transistors_on, TRANSISTORS);

	/* the nodes we are working with */
	nodenum_t list1[NODES];
	list_t listin;

	/* the indirect nodes we are collecting for the next run */
	nodenum_t list2[NODES];
	list_t listout;

	DECLARE_BITMAP(listout_bitmap, NODES);

	nodenum_t group[NODES];
	count_t groupcount;
	DECLARE_BITMAP(groupbitmap, NODES);

	enum {
		contains_nothing,
		contains_hi,
		contains_pullup,
		contains_pulldown,
		contains_vcc,
		contains_vss
	} group_contains_value;
} state_t;

#define INCLUDED_FROM_PERFECT6502_C
#include "perfect6502.h"
#undef INCLUDED_FROM_PERFECT6502_C

/*
 * The "value" propertiy of VCC and GND is never evaluated in the code,
 * so we don't bother initializing it properly or special-casing writes.
 */

static inline void
set_nodes_pullup(state_t *state, transnum_t t, BOOL s)
{
	set_bitmap(state->nodes_pullup, t, s);
}

static inline BOOL
get_nodes_pullup(state_t *state, transnum_t t)
{
	return get_bitmap(state->nodes_pullup, t);
}

static inline void
set_nodes_pulldown(state_t *state, transnum_t t, BOOL s)
{
	set_bitmap(state->nodes_pulldown, t, s);
}

static inline BOOL
get_nodes_pulldown(state_t *state, transnum_t t)
{
	return get_bitmap(state->nodes_pulldown, t);
}

static inline void
set_nodes_value(state_t *state, transnum_t t, BOOL s)
{
	set_bitmap(state->nodes_value, t, s);
}

static inline BOOL
get_nodes_value(state_t *state, transnum_t t)
{
	return get_bitmap(state->nodes_value, t);
}

/************************************************************
 *
 * Data Structures and Algorithms for Transistors
 *
 ************************************************************/

#ifdef BROKEN_TRANSISTORS
unsigned int broken_transistor = (unsigned int)-1;
#endif

static inline void
set_transistors_on(state_t *state, transnum_t t, BOOL s)
{
#ifdef BROKEN_TRANSISTORS
	if (t == broken_transistor)
		return;
#endif
	set_bitmap(state->transistors_on, t, s);
}

static inline BOOL
get_transistors_on(state_t *state, transnum_t t)
{
	return get_bitmap(state->transistors_on, t);
}

/************************************************************
 *
 * Data Structures and Algorithms for Lists
 *
 ************************************************************/

static inline nodenum_t
listin_get(state_t *state, count_t i)
{
	return state->listin.list[i];
}

static inline count_t
listin_count(state_t *state)
{
	return state->listin.count;
}

static inline void
lists_switch(state_t *state)
{
	list_t tmp = state->listin;
	state->listin = state->listout;
	state->listout = tmp;
}

static inline void
listout_clear(state_t *state)
{
	state->listout.count = 0;
	bitmap_clear(state->listout_bitmap, NODES);
}

static inline void
listout_add(state_t *state, nodenum_t i)
{
	if (!get_bitmap(state->listout_bitmap, i)) {
		state->listout.list[state->listout.count++] = i;
		set_bitmap(state->listout_bitmap, i, 1);
	}
}

/************************************************************
 *
 * Data Structures and Algorithms for Groups of Nodes
 *
 ************************************************************/

/*
 * a group is a set of connected nodes, which consequently
 * share the same value
 *
 * we use an array and a count for O(1) insert and
 * iteration, and a redundant bitmap for O(1) lookup
 */

static inline void
group_clear(state_t *state)
{
	state->groupcount = 0;
	bitmap_clear(state->groupbitmap, NODES);
}

static inline void
group_add(state_t *state, nodenum_t i)
{
	state->group[state->groupcount++] = i;
	set_bitmap(state->groupbitmap, i, 1);
}

static inline nodenum_t
group_get(state_t *state, count_t n)
{
	return state->group[n];
}

static inline BOOL
group_contains(state_t *state, nodenum_t el)
{
	return get_bitmap(state->groupbitmap, el);
}

static inline count_t
group_count(state_t *state)
{
	return state->groupcount;
}

/************************************************************
 *
 * Node and Transistor Emulation
 *
 ************************************************************/

static void
addNodeToGroup(state_t *state, nodenum_t n)
{
	/*
	 * We need to stop at vss and vcc, otherwise we'll revisit other groups
	 * with the same value - just because they all derive their value from
	 * the fact that they are connected to vcc or vss.
	 */
	if (n == vss) {
		state->group_contains_value = contains_vss;
		return;
	}
	if (n == vcc) {
		if (state->group_contains_value != contains_vss)
			state->group_contains_value = contains_vcc;
		return;
	}

	if (group_contains(state, n))
		return;

	group_add(state, n);

	if (state->group_contains_value < contains_pulldown && get_nodes_pulldown(state, n)) {
		state->group_contains_value = contains_pulldown;
	}
	if (state->group_contains_value < contains_pullup && get_nodes_pullup(state, n)) {
		state->group_contains_value = contains_pullup;
	}
	if (state->group_contains_value < contains_hi && get_nodes_value(state, n)) {
		state->group_contains_value = contains_hi;
	}

	/* revisit all transistors that control this node */
	for (count_t t = 0; t < state->nodes_c1c2count[n]; t++) {
		transnum_t tn = state->nodes_c1c2s[n][t];
		/* if the transistor connects c1 and c2... */
		if (get_transistors_on(state, tn)) {
			/* if original node was connected to c1, continue with c2 */
			if (state->transistors_c1[tn] == n)
				addNodeToGroup(state, state->transistors_c2[tn]);
			else
				addNodeToGroup(state, state->transistors_c1[tn]);
		}
	}
}

static inline void
addAllNodesToGroup(state_t *state, nodenum_t node)
{
	group_clear(state);

	state->group_contains_value = contains_nothing;

	addNodeToGroup(state, node);
}

static inline BOOL
getGroupValue(state_t *state)
{
	switch (state->group_contains_value) {
		case contains_vcc:
		case contains_pullup:
		case contains_hi:
			return YES;
		case contains_vss:
		case contains_pulldown:
		case contains_nothing:
			return NO;
	}
}

void
recalcNode(state_t *state, nodenum_t node)
{
	/*
	 * get all nodes that are connected through
	 * transistors, starting with this one
	 */
	addAllNodesToGroup(state, node);

	/* get the state of the group */
	BOOL newv = getGroupValue(state);

	/*
	 * - set all nodes to the group state
	 * - check all transistors switched by nodes of the group
	 * - collect all nodes behind toggled transistors
	 *   for the next run
	 */
	for (count_t i = 0; i < group_count(state); i++) {
		nodenum_t nn = group_get(state, i);
		if (get_nodes_value(state, nn) != newv) {
			set_nodes_value(state, nn, newv);
			for (count_t t = 0; t < state->nodes_gatecount[nn]; t++) {
				transnum_t tn = state->nodes_gates[nn][t];
				set_transistors_on(state, tn, newv);
			}

			if (newv) {
				for (count_t g = 0; g < state->nodes_left_dependants[nn]; g++) {
					listout_add(state, state->nodes_left_dependant[nn][g]);
				}
			} else {
				for (count_t g = 0; g < state->nodes_dependants[nn]; g++) {
					listout_add(state, state->nodes_dependant[nn][g]);
				}
			}
		}
	}
}

void
recalcNodeList(state_t *state)
{
	for (int j = 0; j < 100; j++) {	/* loop limiter */
		/*
		 * make the secondary list our primary list, use
		 * the data storage of the primary list as the
		 * secondary list
		 */
		lists_switch(state);

		if (!listin_count(state))
			break;

		listout_clear(state);

		/*
		 * for all nodes, follow their paths through
		 * turned-on transistors, find the state of the
		 * path and assign it to all nodes, and re-evaluate
		 * all transistors controlled by this path, collecting
		 * all nodes that changed because of it for the next run
		 */
		for (count_t i = 0; i < listin_count(state); i++) {
			nodenum_t n = listin_get(state, i);
			recalcNode(state, n);
		}
	}
	listout_clear(state);
}

/************************************************************
 *
 * Node State
 *
 ************************************************************/

static inline void
setNode(state_t *state, nodenum_t nn, BOOL s)
{
	BOOL oldstate = get_nodes_pullup(state, nn);
	if (s != oldstate) {
		set_nodes_pullup(state, nn, s);
		set_nodes_pulldown(state, nn, !s);
		listout_add(state, nn);
	}
}

static inline BOOL
isNodeHigh(state_t *state, nodenum_t nn)
{
	return get_nodes_value(state, nn);
}

/************************************************************
 *
 * Interfacing and Extracting State
 *
 ************************************************************/

#define read8(state, n0,n1,n2,n3,n4,n5,n6,n7) ((uint8_t)(isNodeHigh(state, n0) << 0) | (isNodeHigh(state, n1) << 1) | (isNodeHigh(state, n2) << 2) | (isNodeHigh(state, n3) << 3) | (isNodeHigh(state, n4) << 4) | (isNodeHigh(state, n5) << 5) | (isNodeHigh(state, n6) << 6) | (isNodeHigh(state, n7) << 7))

uint16_t
readAddressBus(state_t *state)
{
	return read8(state, ab0,ab1,ab2,ab3,ab4,ab5,ab6,ab7) | (read8(state, ab8,ab9,ab10,ab11,ab12,ab13,ab14,ab15) << 8);
}

uint8_t
readDataBus(state_t *state)
{
	return read8(state, db0,db1,db2,db3,db4,db5,db6,db7);
}

void
writeDataBus(state_t *state, uint8_t d)
{
	static const nodenum_t dbnodes[8] = { db0, db1, db2, db3, db4, db5, db6, db7 };
	for (int i = 0; i < 8; i++, d>>=1)
		setNode(state, dbnodes[i], d & 1);
}

BOOL
readRW(state_t *state)
{
	return isNodeHigh(state, rw);
}

uint8_t
readA(state_t *state)
{
	return read8(state, a0,a1,a2,a3,a4,a5,a6,a7);
}

uint8_t
readX(state_t *state)
{
	return read8(state, x0,x1,x2,x3,x4,x5,x6,x7);
}

uint8_t
readY(state_t *state)
{
	return read8(state, y0,y1,y2,y3,y4,y5,y6,y7);
}

uint8_t
readP(state_t *state)
{
	return read8(state, p0,p1,p2,p3,p4,p5,p6,p7);
}

uint8_t
readIR(state_t *state)
{
	return read8(state, notir0,notir1,notir2,notir3,notir4,notir5,notir6,notir7) ^ 0xFF;
}

uint8_t
readSP(state_t *state)
{
	return read8(state, s0,s1,s2,s3,s4,s5,s6,s7);
}

uint8_t
readPCL(state_t *state)
{
	return read8(state, pcl0,pcl1,pcl2,pcl3,pcl4,pcl5,pcl6,pcl7);
}

uint8_t
readPCH(state_t *state)
{
	return read8(state, pch0,pch1,pch2,pch3,pch4,pch5,pch6,pch7);
}

uint16_t
readPC(state_t *state)
{
	return (readPCH(state) << 8) | readPCL(state);
}

/************************************************************
 *
 * Tracing/Debugging
 *
 ************************************************************/

unsigned int cycle;

void
chipStatus(state_t *state)
{
	BOOL clk = isNodeHigh(state, clk0);
	uint16_t a = readAddressBus(state);
	uint8_t d = readDataBus(state);
	BOOL r_w = isNodeHigh(state, rw);

	printf("halfcyc:%d phi0:%d AB:%04X D:%02X RnW:%d PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X IR:%02X",
			cycle,
			clk,
			a,
	        d,
	        r_w,
			readPC(state),
			readA(state),
			readX(state),
			readY(state),
			readSP(state),
			readP(state),
			readIR(state));

	if (clk) {
		if (r_w)
			printf(" R$%04X=$%02X", a, memory[a]);
		else
			printf(" W$%04X=$%02X", a, d);
	}
	printf("\n");
}

/************************************************************
 *
 * Address Bus and Data Bus Interface
 *
 ************************************************************/

uint8_t memory[65536];

uint8_t mRead(uint16_t a)
{
	return memory[a];
}

void
mWrite(uint16_t a, uint8_t d)
{
	memory[a] = d;
}

static inline void
handleMemory(state_t *state)
{
	if (isNodeHigh(state, rw))
		writeDataBus(state, mRead(readAddressBus(state)));
	else
		mWrite(readAddressBus(state), readDataBus(state));
}

/************************************************************
 *
 * Main Clock Loop
 *
 ************************************************************/

void
step(state_t *state)
{
	BOOL clk = isNodeHigh(state, clk0);

	/* invert clock */
	setNode(state, clk0, !clk);
	recalcNodeList(state);

	/* handle memory reads and writes */
	if (!clk)
		handleMemory(state);

	cycle++;
}

/************************************************************
 *
 * Initialization
 *
 ************************************************************/

unsigned int transistors;

static inline void
add_nodes_dependant(state_t *state, nodenum_t a, nodenum_t b)
{
	for (count_t g = 0; g < state->nodes_dependants[a]; g++)
		if (state->nodes_dependant[a][g] == b)
			return;

	state->nodes_dependant[a][state->nodes_dependants[a]++] = b;
}

static inline void
add_nodes_left_dependant(state_t *state, nodenum_t a, nodenum_t b)
{
	for (count_t g = 0; g < state->nodes_left_dependants[a]; g++)
		if (state->nodes_left_dependant[a][g] == b)
			return;

	state->nodes_left_dependant[a][state->nodes_left_dependants[a]++] = b;
}

void
setupNodesAndTransistors(state_t *state)
{
	count_t i;
	/* copy nodes into r/w data structure */
	for (i = 0; i < NODES; i++) {
		set_nodes_pullup(state, i, segdefs[i] == 1);
		state->nodes_gatecount[i] = 0;
		state->nodes_c1c2count[i] = 0;
	}
	/* copy transistors into r/w data structure */
	count_t j = 0;
	for (i = 0; i < TRANSISTORS; i++) {
		nodenum_t gate = transdefs[i].gate;
		nodenum_t c1 = transdefs[i].c1;
		nodenum_t c2 = transdefs[i].c2;
		/* skip duplicate transistors */
		BOOL found = NO;
#ifndef BROKEN_TRANSISTORS
		for (count_t k = 0; k < i; k++) {
			if (transdefs[k].gate == gate && 
			    ((transdefs[k].c1 == c1 && 
			    transdefs[k].c2 == c2) ||
			    (transdefs[k].c1 == c2 && 
			    transdefs[k].c2 == c1))) {
				found = YES;
				break; 
			}
		}
#endif
		if (!found) {
			state->transistors_gate[j] = gate;
			state->transistors_c1[j] = c1;
			state->transistors_c2[j] = c2;
			j++;
		}
	}
	transistors = j;
#ifdef DEBUG
	printf("transistors: %d\n", transistors);
#endif

	/* cross reference transistors in nodes data structures */
	for (i = 0; i < transistors; i++) {
		nodenum_t gate = state->transistors_gate[i];
		nodenum_t c1 = state->transistors_c1[i];
		nodenum_t c2 = state->transistors_c2[i];
		state->nodes_gates[gate][state->nodes_gatecount[gate]++] = i;
		state->nodes_c1c2s[c1][state->nodes_c1c2count[c1]++] = i;
		state->nodes_c1c2s[c2][state->nodes_c1c2count[c2]++] = i;
	}

	for (i = 0; i < NODES; i++) {
		state->nodes_dependants[i] = 0;
		state->nodes_left_dependants[i] = 0;
		for (count_t g = 0; g < state->nodes_gatecount[i]; g++) {
			transnum_t t = state->nodes_gates[i][g];
			nodenum_t c1 = state->transistors_c1[t];
			if (c1 != vss && c1 != vcc) {
				add_nodes_dependant(state, i, c1);
			}
			nodenum_t c2 = state->transistors_c2[t];
			if (c2 != vss && c2 != vcc) {
				add_nodes_dependant(state, i, c2);
			}
			if (c1 != vss && c1 != vcc) {
				add_nodes_left_dependant(state, i, c1);
			} else {
				add_nodes_left_dependant(state, i, c2);
			}
		}
	}
}

void
resetChip(state_t *state)
{
	/* all nodes are down */
	for (nodenum_t nn = 0; nn < NODES; nn++) {
		set_nodes_value(state, nn, 0);
	}
	/* all transistors are off */
	for (transnum_t tn = 0; tn < TRANSISTORS; tn++) 
		set_transistors_on(state, tn, NO);

	setNode(state, res, 0);
	setNode(state, clk0, 1);
	setNode(state, rdy, 1);
	setNode(state, so, 0);
	setNode(state, irq, 1);
	setNode(state, nmi, 1);

	for (count_t i = 0; i < NODES; i++)
		listout_add(state, i);

	recalcNodeList(state);

	/* hold RESET for 8 cycles */
	for (int i = 0; i < 16; i++)
		step(state);

	/* release RESET */
	setNode(state, res, 1);
	recalcNodeList(state);

	cycle = 0;
}

state_t *
initAndResetChip()
{
	state_t *state = malloc(sizeof(state_t));

	state->listin.list = state->list1;
	state->listout.list = state->list2;

	/* set up data structures for efficient emulation */
	setupNodesAndTransistors(state);

	/* set initial state of nodes, transistors, inputs; RESET chip */
	resetChip(state);

	return state;
}
