/*
 Copyright (c) 2010,2014 Michael Steil, Brian Silverman, Barry Silverman

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

/* nodes & transistors */
#include "netlist_6502.h"
/* node numbers of probes */
#include "nodenames.h"

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
	nodenum_t nodes;
	nodenum_t transistors;

	/* everything that describes a node */
	bitmap_t *nodes_pullup;
	bitmap_t *nodes_pulldown;
	bitmap_t *nodes_value;
	nodenum_t **nodes_gates;
	nodenum_t **nodes_c1c2s;
	count_t *nodes_gatecount;
	count_t *nodes_c1c2count;
	nodenum_t *nodes_dependants;
	nodenum_t *nodes_left_dependants;
	nodenum_t **nodes_dependant;
	nodenum_t **nodes_left_dependant;

	/* everything that describes a transistor */
	nodenum_t *transistors_gate;
	nodenum_t *transistors_c1;
	nodenum_t *transistors_c2;
	bitmap_t *transistors_on;

	/* the nodes we are working with */
	nodenum_t *list1;
	list_t listin;

	/* the indirect nodes we are collecting for the next run */
	nodenum_t *list2;
	list_t listout;

	bitmap_t *listout_bitmap;

	nodenum_t *group;
	count_t groupcount;
	bitmap_t *groupbitmap;

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

static inline void
set_transistors_on(state_t *state, transnum_t t, BOOL s)
{
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
	bitmap_clear(state->listout_bitmap, state->nodes);
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
	bitmap_clear(state->groupbitmap, state->nodes);
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

static inline unsigned int
readNodes(state_t *state, int count, nodenum_t *nodelist)
{
	int result = 0;
	for (int i = count - 1; i >= 0; i--) {
		result <<=  1;
		result |= isNodeHigh(state, nodelist[i]);
	}
	return result;
}

void
writeNodes(state_t *state, int count, nodenum_t *nodelist, int v)
{
	for (int i = 0; i < 8; i++, v >>= 1)
	setNode(state, nodelist[i], v & 1);
}

/************************************************************
 *
 * 6502-specific Interfacing
 *
 ************************************************************/

uint16_t
readAddressBus(state_t *state)
{
	return readNodes(state, 16, (nodenum_t[]){ ab0, ab1, ab2, ab3, ab4, ab5, ab6, ab7, ab8, ab9, ab10, ab11, ab12, ab13, ab14, ab15 });
}

uint8_t
readDataBus(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ db0, db1, db2, db3, db4, db5, db6, db7 });
}

void
writeDataBus(state_t *state, uint8_t d)
{
	writeNodes(state, 8, (nodenum_t[]){ db0, db1, db2, db3, db4, db5, db6, db7 }, d);
}

BOOL
readRW(state_t *state)
{
	return isNodeHigh(state, rw);
}

uint8_t
readA(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ a0,a1,a2,a3,a4,a5,a6,a7 });
}

uint8_t
readX(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ x0,x1,x2,x3,x4,x5,x6,x7 });
}

uint8_t
readY(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ y0,y1,y2,y3,y4,y5,y6,y7 });
}

uint8_t
readP(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ p0,p1,p2,p3,p4,p5,p6,p7 });
}

uint8_t
readIR(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ notir0,notir1,notir2,notir3,notir4,notir5,notir6,notir7 }) ^ 0xFF;
}

uint8_t
readSP(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ s0,s1,s2,s3,s4,s5,s6,s7 });
}

uint8_t
readPCL(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ pcl0,pcl1,pcl2,pcl3,pcl4,pcl5,pcl6,pcl7 });
}

uint8_t
readPCH(state_t *state)
{
	return readNodes(state, 8, (nodenum_t[]){ pch0,pch1,pch2,pch3,pch4,pch5,pch6,pch7 });
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

static uint8_t
mRead(uint16_t a)
{
	return memory[a];
}

static void
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

static state_t *
setupNodesAndTransistors(netlist_transdefs *transdefs, BOOL *node_is_pullup, nodenum_t nodes, nodenum_t transistors)
{
	/* allocate state */
	state_t *state = malloc(sizeof(state_t));
	state->nodes = nodes;
	state->transistors = transistors;
	state->nodes_pullup = malloc(WORDS_FOR_BITS(state->nodes) * sizeof(*state->nodes_pullup));
	state->nodes_pulldown = malloc(WORDS_FOR_BITS(state->nodes) * sizeof(*state->nodes_pulldown));
	state->nodes_value = malloc(WORDS_FOR_BITS(state->nodes) * sizeof(*state->nodes_value));
	state->nodes_gates = malloc(state->nodes * sizeof(*state->nodes_gates));
	for (count_t i = 0; i < state->nodes; i++) {
		state->nodes_gates[i] = malloc(state->nodes * sizeof(**state->nodes_gates));
	}
	state->nodes_c1c2s = malloc(state->nodes * sizeof(*state->nodes_c1c2s));
	for (count_t i = 0; i < state->nodes; i++) {
		state->nodes_c1c2s[i] = malloc(2 * state->nodes * sizeof(**state->nodes_c1c2s));
	}
	state->nodes_gatecount = malloc(state->nodes * sizeof(*state->nodes_gatecount));
	state->nodes_c1c2count = malloc(state->nodes * sizeof(*state->nodes_c1c2count));
	state->nodes_dependants = malloc(state->nodes * sizeof(*state->nodes_dependants));
	state->nodes_left_dependants = malloc(state->nodes * sizeof(*state->nodes_left_dependants));
	state->nodes_dependant = malloc(state->nodes * sizeof(*state->nodes_dependant));
	for (count_t i = 0; i < state->nodes; i++) {
		state->nodes_dependant[i] = malloc(state->nodes * sizeof(**state->nodes_dependant));
	}
	state->nodes_left_dependant = malloc(state->nodes * sizeof(*state->nodes_left_dependant));
	for (count_t i = 0; i < state->nodes; i++) {
		state->nodes_left_dependant[i] = malloc(state->nodes * sizeof(**state->nodes_left_dependant));
	}
	state->transistors_gate = malloc(state->transistors * sizeof(*state->transistors_gate));
	state->transistors_c1 = malloc(state->transistors * sizeof(*state->transistors_c1));
	state->transistors_c2 = malloc(state->transistors * sizeof(*state->transistors_c2));
	state->transistors_on = malloc(WORDS_FOR_BITS(state->transistors) * sizeof(*state->transistors_on));
	state->list1 = malloc(state->nodes * sizeof(*state->list1));
	state->list2 = malloc(state->nodes * sizeof(*state->list2));
	state->listout_bitmap = malloc(WORDS_FOR_BITS(state->nodes) * sizeof(*state->listout_bitmap));
	state->group = malloc(state->nodes * sizeof(*state->group));
	state->groupbitmap = malloc(WORDS_FOR_BITS(state->nodes) * sizeof(*state->groupbitmap));
	state->listin.list = state->list1;
	state->listout.list = state->list2;

	count_t i;
	/* copy nodes into r/w data structure */
	for (i = 0; i < state->nodes; i++) {
		set_nodes_pullup(state, i, node_is_pullup[i]);
		state->nodes_gatecount[i] = 0;
		state->nodes_c1c2count[i] = 0;
	}
	/* copy transistors into r/w data structure */
	count_t j = 0;
	for (i = 0; i < state->transistors; i++) {
		nodenum_t gate = transdefs[i].gate;
		nodenum_t c1 = transdefs[i].c1;
		nodenum_t c2 = transdefs[i].c2;
		/* skip duplicate transistors */
		BOOL found = NO;
		for (count_t j2 = 0; j2 < j; j2++) {
			if (state->transistors_gate[j2] == gate &&
				((state->transistors_c1[j2] == c1 &&
				state->transistors_c2[j2] == c2) ||
				(state->transistors_c1[j2] == c2 &&
				 state->transistors_c2[j2] == c1))) {
				found = YES;
			}
		}
		if (!found) {
			state->transistors_gate[j] = gate;
			state->transistors_c1[j] = c1;
			state->transistors_c2[j] = c2;
			j++;
		}
	}
	state->transistors = j;

	/* cross reference transistors in nodes data structures */
	for (i = 0; i < state->transistors; i++) {
		nodenum_t gate = state->transistors_gate[i];
		nodenum_t c1 = state->transistors_c1[i];
		nodenum_t c2 = state->transistors_c2[i];
		state->nodes_gates[gate][state->nodes_gatecount[gate]++] = i;
		state->nodes_c1c2s[c1][state->nodes_c1c2count[c1]++] = i;
		state->nodes_c1c2s[c2][state->nodes_c1c2count[c2]++] = i;
	}

	for (i = 0; i < state->nodes; i++) {
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

	return state;
}

void
resetChip(state_t *state)
{
#if 0 /* unnecessary - RESET will stabilize the network anyway */
	/* all nodes are down */
	for (nodenum_t nn = 0; nn < state->nodes; nn++) {
		set_nodes_value(state, nn, 0);
	}
	/* all transistors are off */
	for (transnum_t tn = 0; tn < state->transistors; tn++)
		set_transistors_on(state, tn, NO);
#endif

	setNode(state, res, 0);
	setNode(state, clk0, 1);
	setNode(state, rdy, 1);
	setNode(state, so, 0);
	setNode(state, irq, 1);
	setNode(state, nmi, 1);

	for (count_t i = 0; i < state->nodes; i++)
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
	/* set up data structures for efficient emulation */
	nodenum_t nodes = sizeof(netlist_6502_node_is_pullup)/sizeof(*netlist_6502_node_is_pullup);
	nodenum_t transistors = sizeof(netlist_6502_transdefs)/sizeof(*netlist_6502_transdefs);
	state_t *state = setupNodesAndTransistors(netlist_6502_transdefs,
											  netlist_6502_node_is_pullup,
											  nodes,
											  transistors);

	/* set initial state of nodes, transistors, inputs; RESET chip */
	resetChip(state);

	return state;
}
