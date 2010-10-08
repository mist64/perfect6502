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

#include <dispatch/dispatch.h>

#include "perfect6502.h"

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
nodenum_t nodes_dependants[NODES];
nodenum_t nodes_dependant[NODES][NODES];

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

#ifdef BROKEN_TRANSISTORS
unsigned int broken_transistor = (unsigned int)-1;
#endif

static inline void
set_transistors_on(transnum_t t, BOOL state)
{
#ifdef BROKEN_TRANSISTORS
	if (t == broken_transistor)
		return;
#endif
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
} list_t;

/* the nodes we are working with */
nodenum_t list1[NODES];
list_t listin = {
	.list = list1,
};

/* the indirect nodes we are collecting for the next run */
nodenum_t list2[NODES];
list_t listout = {
	.list = list2,
};

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
}

static inline void
listout_add(nodenum_t i)
{
	listout.list[listout.count++] = i;
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
 * Node and Transistor Emulation
 *
 ************************************************************/

BOOL group_contains_pullup;
BOOL group_contains_pulldown;
BOOL group_contains_hi;

static void
addNodeToGroup(nodenum_t n)
{
	if (group_contains(n))
		return;

	group_add(n);

	if (get_nodes_pullup(n))
		group_contains_pullup = YES;
	if (get_nodes_pulldown(n))
		group_contains_pulldown = YES;
	if (get_nodes_value(n))
		group_contains_hi = YES;

	if (n == vss || n == vcc)
		return;

	/* revisit all transistors that are controlled by this node */
	for (count_t t = 0; t < nodes_c1c2count[n]; t++) {
		transnum_t tn = nodes_c1c2s[n][t];
		/* if the transistor connects c1 and c2... */
		if (get_transistors_on(tn)) {
			/* if original node was connected to c1, continue with c2 */
			if (transistors_c1[tn] == n)
				addNodeToGroup(transistors_c2[tn]);
			else
				addNodeToGroup(transistors_c1[tn]);
		}
	}
}

static inline void
addAllNodesToGroup(node)
{
	group_clear();

	group_contains_pullup = NO;
	group_contains_pulldown = NO;
	group_contains_hi = NO;

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

void
recalcNode(nodenum_t node)
{
	/*
	 * get all nodes that are connected through
	 * transistors, starting with this one
	 */
	addAllNodesToGroup(node);

#ifdef DEBUG
printf("  %s node %d -> ", __func__, node);
	for (count_t j = 0; j < group_count(); j++)
		printf("%d ", group_get(j));
	printf("\n");
#endif

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
			for (count_t t = 0; t < nodes_gatecount[nn]; t++) {
				transnum_t tn = nodes_gates[nn][t];
				set_transistors_on(tn, !get_transistors_on(tn));
			}
			listout_add(nn);
		}
	}
}

void
recalcNodeList(const nodenum_t *source, count_t count)
{
	listout_clear();

	for (count_t i = 0; i < count; i++)
		recalcNode(source[i]);

	lists_switch();

	for (int j = 0; j < 100; j++) {	/* loop limiter */
#ifdef DEBUG
		printf("%s iteration=%d, count=%d\n", __func__, j, listin_count());
#endif
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
		for (count_t i = 0; i < listin_count(); i++) {
			nodenum_t n = listin_get(i);
#ifdef DEBUG
printf("libdispatch %d times\n", nodes_dependants[n]);
#endif
#if 1
			for (count_t g = 0; g < nodes_dependants[n]; g++) {
#else
			dispatch_apply(nodes_dependants[n], dispatch_get_global_queue(0, 0), ^(size_t g) {
#endif
				recalcNode(nodes_dependant[n][g]);
			}
#if 1
#else
);
#endif
		}
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
 * Node State
 *
 ************************************************************/

static inline void
setNode(nodenum_t nn, BOOL state)
{
	set_nodes_pullup(nn, state);
	set_nodes_pulldown(nn, !state);
	recalcNodeList(&nn, 1);
}

static inline BOOL
isNodeHigh(nodenum_t nn)
{
	return get_nodes_value(nn);
}

/************************************************************
 *
 * Interfacing and Extracting State
 *
 ************************************************************/

#define read8(n0,n1,n2,n3,n4,n5,n6,n7) ((uint8_t)(isNodeHigh(n0) << 0) | (isNodeHigh(n1) << 1) | (isNodeHigh(n2) << 2) | (isNodeHigh(n3) << 3) | (isNodeHigh(n4) << 4) | (isNodeHigh(n5) << 5) | (isNodeHigh(n6) << 6) | (isNodeHigh(n7) << 7))

uint16_t
readAddressBus()
{
	return read8(ab0,ab1,ab2,ab3,ab4,ab5,ab6,ab7) | (read8(ab8,ab9,ab10,ab11,ab12,ab13,ab14,ab15) << 8);
}

uint8_t
readDataBus()
{
	return read8(db0,db1,db2,db3,db4,db5,db6,db7);
}

void
writeDataBus(uint8_t d)
{
	static const nodenum_t dbnodes[8] = { db0, db1, db2, db3, db4, db5, db6, db7 };
	for (int i = 0; i < 8; i++, d>>=1)
		setNode(dbnodes[i], d & 1);
}

BOOL
readRW()
{
	return isNodeHigh(rw);
}

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
readIR()
{
	return read8(notir0,notir1,notir2,notir3,notir4,notir5,notir6,notir7) ^ 0xFF;
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

/************************************************************
 *
 * Tracing/Debugging
 *
 ************************************************************/

unsigned int cycle;

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
			readIR());

	if (clk)
		if (r_w)
			printf(" R$%04X=$%02X", a, memory[a]);
		else
			printf(" W$%04X=$%02X", a, d);
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
handleMemory()
{
	if (isNodeHigh(rw))
		writeDataBus(mRead(readAddressBus()));
	else
		mWrite(readAddressBus(), readDataBus());
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

#if 0
	int total = 0;
	for (count_t i = 0; i < NODES; i++) {
		addAllNodesToGroup(i);
		printf("%d: ", i);
		total += group_count();
		for (count_t j = 0; j < group_count(); j++) {
			printf("%d ", group_get(j));
		}
		printf("\n");
	}
	printf("TOTAL %f\n", ((float)total)/NODES);
#endif
}

/************************************************************
 *
 * Initialization
 *
 ************************************************************/

unsigned int transistors;

static inline void
add_nodes_dependant(nodenum_t a, nodenum_t b)
{
	for (count_t g = 0; g < nodes_dependants[a]; g++)
		if (nodes_dependant[a][g] == b)
			return;

	nodes_dependant[a][nodes_dependants[a]++] = b;
}

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
			transistors_gate[j] = gate;
			transistors_c1[j] = c1;
			transistors_c2[j] = c2;
			j++;
		}
	}
	transistors = j;
#ifdef DEBUG
	printf("transistors: %d\n", transistors);
#endif

	/* cross reference transistors in nodes data structures */
	for (i = 0; i < transistors; i++) {
		nodenum_t gate = transistors_gate[i];
		nodenum_t c1 = transistors_c1[i];
		nodenum_t c2 = transistors_c2[i];
		nodes_gates[gate][nodes_gatecount[gate]++] = i;
		nodes_c1c2s[c1][nodes_c1c2count[c1]++] = i;
		nodes_c1c2s[c2][nodes_c1c2count[c2]++] = i;
	}

	for (i = 0; i < NODES; i++) {
		nodes_dependants[i] = 0;
		for (count_t g = 0; g < nodes_gatecount[i]; g++) {
			transnum_t t = nodes_gates[i][g];
			add_nodes_dependant(i, transistors_c1[t]);
			add_nodes_dependant(i, transistors_c2[t]);
		}
	}

#ifdef DEBUG
	for (i = 0; i < NODES; i++) {
		printf("%d: ", i);
		for (count_t g = 0; g < nodes_dependants[i]; g++) {
			printf("%d ", nodes_dependant[i][g]);
		}
		printf("(%d)\n", nodes_dependants[i]);
	}
#endif
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

	setNode(res, 0);
	setNode(clk0, 1);
	setNode(rdy, 1);
	setNode(so, 0);
	setNode(irq, 1);
	setNode(nmi, 1);

	recalcAllNodes(); 

	/* hold RESET for 8 cycles */
	for (int i = 0; i < 16; i++)
		step();

	/* release RESET */
	setNode(res, 1);

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
