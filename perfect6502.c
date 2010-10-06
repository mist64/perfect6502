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

//#define TEST
//#define BROKEN_TRANSISTORS
//#define COMPARE

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

/* everything that describes a node */
DECLARE_BITMAP(nodes_pullup, NODES);
DECLARE_BITMAP(nodes_pulldown, NODES);
DECLARE_BITMAP(nodes_value, NODES);
nodenum_t nodes_gates[NODES][NODES];
nodenum_t nodes_c1c2s[NODES][2*NODES];
count_t nodes_gatecount[NODES];
count_t nodes_c1c2count[NODES];

/*
 * The "value" propertiy of VCC and GND is never evaluated in the code,
 * so we don't bother initializing it properly or special-casing writes.
 */

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
set_nodes_value(transnum_t t, BOOL state)
{
	set_bitmap(nodes_value, t, state);
}

static inline BOOL
get_nodes_value(transnum_t t)
{
	return get_bitmap(nodes_value, t);
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

static inline BOOL
listout_contains(nodenum_t el)
{
	return get_bitmap(listout.bitmap, el);
}

static inline void
listout_add(nodenum_t i)
{
	if (!listout_contains(i)) {
		listout.list[listout.count++] = i;
		set_bitmap(listout.bitmap, i, 1);
	}
}

/************************************************************
 *
 * Data Structures and Algorithms for Groups of Nodes
 *
 ************************************************************/

/*
 * a group is a set of connected nodes, which consequently
 * share the same potential
 *
 * we use an array and a count for O(1) insert and
 * iteration, and a redundant bitmap for O(1) lookup
 */
static nodenum_t group[NODES];
static count_t groupcount;
DECLARE_BITMAP(groupbitmap, NODES);

BOOL group_contains_pullup;
BOOL group_contains_pulldown;
BOOL group_contains_hi;

static inline void
group_clear()
{
	groupcount = 0;
	bitmap_clear(groupbitmap, NODES);
	group_contains_pullup = NO;
	group_contains_pulldown = NO;
	group_contains_hi = NO;
}

static inline void
group_add(nodenum_t i)
{
	group[groupcount++] = i;
	set_bitmap(groupbitmap, i, 1);

	if (get_nodes_pullup(i))
		group_contains_pullup = YES;
	if (get_nodes_pulldown(i))
		group_contains_pulldown = YES;
	if (get_nodes_value(i))
		group_contains_hi = YES;
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
	return get_nodes_value(nn);
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

	/* revisit all transistors that are controlled by this node */
	if (i != vss && i != vcc)
		for (count_t t = 0; t < nodes_c1c2count[i]; t++)
			addNodeTransistor(i, nodes_c1c2s[i][t]);
}

static inline void
addAllNodesToGroup(node)
{
	group_clear();
	addNodeToGroup(node);
}

static inline BOOL
getGroupValue()
{
	if (group_contains(vss))
		return NO;

	if (group_contains(vcc))
		return YES;

	if (group_contains_pulldown)
		return NO;

	if (group_contains_pullup)
		return YES;

	return group_contains_hi;
}

#ifdef BROKEN_TRANSISTORS
transnum_t broken_transistor = (transnum_t)-1;
#endif

void
toggleTransistor(transnum_t tn)
{
#ifdef BROKEN_TRANSISTORS
	if (tn == broken_transistor) {
		if (!get_transistors_on(tn))
			return;
	}
#endif
	set_transistors_on(tn, !get_transistors_on(tn));

	/* next time, we'll have to look at both nodes behind the transistor */
	listout_add(transistors_c1[tn]);
	listout_add(transistors_c2[tn]);
}

void
recalcNode(nodenum_t node)
{
	/*
	 * get all nodes that are connected through
	 * transistors, starting with this one
	 */
	addAllNodesToGroup(node);

	/* get the state of the group */
	BOOL newv = getGroupValue();

	/*
	 * - set all nodes to the group state
	 * - check all transistors switched by nodes of the group
	 * - collect all nodes behind toggled transistors
	 *   for the next run
	 */
	for (count_t i = 0; i < group_count(); i++) {
		nodenum_t nn = group_get(i);
		if (get_nodes_value(nn) != newv) {
			set_nodes_value(nn, newv);
			for (count_t t = 0; t < nodes_gatecount[nn]; t++)
				toggleTransistor(nodes_gates[nn][t]);
		}
	}
}

void
recalcNodeList(const nodenum_t *source, count_t count)
{
	listin_fill(source, count);

	int j;
	for (j = 0; j < 100; j++) {	/* loop limiter */
		if (!listin_count())
			break;

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

uint8_t memory[65536];

/* the nodes that make the data bus */
const nodenum_t dbnodes[8] = { db0, db1, db2, db3, db4, db5, db6, db7 };

void
writeDataBus(uint8_t d)
{
	for (int i = 0; i < 8; i++) {
		setNode(dbnodes[i], d & 1);
		d >>= 1;
	}

	/* recalc all nodes connected starting from the data bus */
	recalcNodeList(dbnodes, 8);
}

uint8_t mRead(uint16_t a)
{
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
	memory[a] = d;
}

static inline void
handleMemory()
{
	if (isNodeHigh(rw))
		writeDataBus(mRead(readAddressBus()));
	else
		mWrite(readAddressBus(), readDataBus());
}

/************************************************************
 *
 * Tracing/Debugging
 *
 ************************************************************/

#define read8(n0,n1,n2,n3,n4,n5,n6,n7) (isNodeHigh(n0) << 0) | (isNodeHigh(n1) << 1) | (isNodeHigh(n2) << 2) | (isNodeHigh(n3) << 3) | (isNodeHigh(n4) << 4) | (isNodeHigh(n5) << 5) | (isNodeHigh(n6) << 6) | (isNodeHigh(n7) << 7)

uint8_t
readA()
{
	return read8(a0,a1,a2,a3,a4,a5,a6,a7);
}

uint8_t
readX()
{
	return read8(x0,x1,x2,x3,x4,x5,x6,x7);
}

uint8_t
readY()
{
	return read8(y0,y1,y2,y3,y4,y5,y6,y7);
}

uint8_t
readP()
{
	return read8(p0,p1,p2,p3,p4,p5,p6,p7);
}

uint8_t
readNOTIR()
{
	return read8(notir0,notir1,notir2,notir3,notir4,notir5,notir6,notir7);
}

uint8_t
readSP()
{
	return read8(s0,s1,s2,s3,s4,s5,s6,s7);
}

uint8_t
readPCL()
{
	return read8(pcl0,pcl1,pcl2,pcl3,pcl4,pcl5,pcl6,pcl7);
}

uint8_t
readPCH()
{
	return read8(pch0,pch1,pch2,pch3,pch4,pch5,pch6,pch7);
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
	BOOL clk = isNodeHigh(clk0);
	uint16_t a = readAddressBus();
	uint8_t d = readDataBus();
	BOOL r_w = isNodeHigh(rw);

	printf("halfcyc:%d phi0:%d AB:%04X D:%02X RnW:%d PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X IR:%02X",
			cycle,
			clk,
			a,
	        d,
	        r_w,
			readPC(),
			readA(),
			readX(),
			readY(),
			readSP(),
			readP(),
			readNOTIR() ^ 0xFF);

	if (clk)
		if (r_w)
			printf(" R$%04X=$%02X", a, memory[a]);
		else
			printf(" W$%04X=$%02X", a, d);
	printf("\n");
}

/************************************************************
 *
 * Main Clock Loop
 *
 ************************************************************/

void
step()
{
	BOOL clk = isNodeHigh(clk0);

	/* invert clock */
	setNode(clk0, !clk);

	/* handle memory reads and writes */
	if (!clk)
		handleMemory();

	cycle++;
}

/************************************************************
 *
 * Initialization
 *
 ************************************************************/

count_t transistors;

void
setupNodesAndTransistors()
{
	count_t i;
	/* copy nodes into r/w data structure */
	for (i = 0; i < NODES; i++) {
		set_nodes_pullup(i, segdefs[i] == 1);
		nodes_gatecount[i] = 0;
		nodes_c1c2count[i] = 0;
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
			    transdefs[k].c1 == c1 && 
			    transdefs[k].c2 == c2) {
				found = YES;
				break; 
			}
		}
#endif
		if (!found) {
			transistors_gate[j] = gate;
			transistors_c1[j] = c1;
			transistors_c2[j] = c2;
			j++;
		}
	}
	transistors = j;

	/* cross reference transistors in nodes data structures */
	for (i = 0; i < transistors; i++) {
		nodenum_t gate = transistors_gate[i];
		nodenum_t c1 = transistors_c1[i];
		nodenum_t c2 = transistors_c2[i];
		nodes_gates[gate][nodes_gatecount[gate]++] = i;
		nodes_c1c2s[c1][nodes_c1c2count[c1]++] = i;
		nodes_c1c2s[c2][nodes_c1c2count[c2]++] = i;
	}
}

void
resetChip()
{
	/* all nodes are down */
	for (nodenum_t nn = 0; nn < NODES; nn++) {
		set_nodes_value(nn, 0);
	}
	/* all transistors are off */
	for (transnum_t tn = 0; tn < TRANSISTORS; tn++) 
		set_transistors_on(tn, NO);

	setLow(res);
	setHigh(clk0);
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

	cycle = 0;
}

void
initAndResetChip()
{
	/* set up data structures for efficient emulation */
	setupNodesAndTransistors();

	/* set initial state of nodes, transistors, inputs; RESET chip */
	resetChip();
}

/************************************************************
 *
 * Main
 *
 ************************************************************/

void init_monitor();
void handle_monitor();

#ifdef TEST
#include "test.c"
#elif defined(BROKEN_TRANSISTORS)
#include "broken_transistors.c"
#elif defined(COMPARE)
#include "compare.c"
#else
#endif
