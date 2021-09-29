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

/************************************************************
 *
 * Libc Functions and Basic Data Types
 *
 ************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"

/* the smallest types to fit the numbers */
typedef uint16_t transnum_t;
typedef uint16_t count_t;
/* nodenum_t is declared in types.h, because it's API */

/************************************************************
 *
 * Main State Data Structure
 *
 ************************************************************/

#if 1       /* faster on 64 bit CPUs */
typedef unsigned long long bitmap_t;
#define BITMAP_SHIFT 6
#define BITMAP_MASK 63
#define ONE 1ULL

#elif 0       /* faster on most 32 bit CPUs */
typedef uint32_t bitmap_t;
#define BITMAP_SHIFT 5
#define BITMAP_MASK 31
#define ONE 1UL

#else       /* faster in some cases (and compilers) that allow for vectorization */
typedef uint8_t bitmap_t;
#define BITMAP_SHIFT 3
#define BITMAP_MASK 7
#define ONE 1U

#endif

/* list of nodes that need to be recalculated */
typedef struct {
	nodenum_t *list;
	count_t count;
} list_t;

/* a transistor from the point of view of one of the connected nodes */
typedef struct {
	transnum_t transistor;
	nodenum_t other_node;
} c1c2_t;

static inline c1c2_t
c1c2(transnum_t tn, nodenum_t n)
{
	c1c2_t c = { tn, n };
	return c;
}

typedef struct {
	nodenum_t nodes;
	nodenum_t transistors;
	nodenum_t vss;
	nodenum_t vcc;

	/* everything that describes a node */
	bitmap_t *nodes_pullup;
	bitmap_t *nodes_pulldown;
	bitmap_t *nodes_value;
	nodenum_t *nodes_gates;
    nodenum_t *node_block;
	c1c2_t *nodes_c1c2s;
	count_t *nodes_c1c2offset;
	nodenum_t *nodes_dependant;
	nodenum_t *nodes_left_dependant;
    nodenum_t *dependent_block;
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

} state_t;

typedef enum {
        contains_nothing = 0,
        contains_hi = 1,
        contains_pullup = 2,
        contains_pulldown = 3,
        contains_vcc = 4,
        contains_vss = 5
} group_value;

/************************************************************
 *
 * Main Header Include
 *
 ************************************************************/

#define INCLUDED_FROM_NETLIST_SIM_C
#include "netlist_sim.h"
#undef INCLUDED_FROM_NETLIST_SIM_C

/************************************************************
 *
 * Algorithms for Bitmaps
 *
 ************************************************************/

#define WORDS_FOR_BITS(a) (a / (sizeof(bitmap_t) * 8) + 1)

static inline void
bitmap_clear(bitmap_t *bitmap, count_t count)
{
	memset(bitmap, 0, WORDS_FOR_BITS(count)*sizeof(bitmap_t));
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
 * Algorithms for Nodes
 *
 ************************************************************/

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
 * Algorithms for Transistors
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
 * Algorithms for Lists
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
	if (get_bitmap(state->listout_bitmap, i) == 0) {
		state->listout.list[state->listout.count++] = i;
		set_bitmap(state->listout_bitmap, i, 1);
	}
}

/************************************************************
 *
 * Algorithms for Groups of Nodes
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

static inline group_value
addNodeToGroup(state_t *state, nodenum_t n, group_value val)
{
	/*
	 * We need to stop at vss and vcc, otherwise we'll revisit other groups
	 * with the same value - just because they all derive their value from
	 * the fact that they are connected to vcc or vss.
	 */
	if (n == state->vss) {
		return contains_vss;
	}
    
	if (n == state->vcc) {
		if (val != contains_vss)
			val = contains_vcc;
		return val;
	}

    /* check and see if we already have this node, and if so return */
	if (group_contains(state, n))
		return val;

	group_add(state, n);

	if (val < contains_pulldown && get_nodes_pulldown(state, n))
		val = contains_pulldown;
	if (val < contains_pullup && get_nodes_pullup(state, n))
		val = contains_pullup;
	if (val < contains_hi && get_nodes_value(state, n))
		val = contains_hi;
    /* state can remain at contains_nothing if the node value is low */

	/* revisit all transistors that control this node */
    const count_t start = state->nodes_c1c2offset[n];
	const count_t end = state->nodes_c1c2offset[n+1];
    const c1c2_t *node_c1c2s = state->nodes_c1c2s;
	for (count_t t = start; t < end; t++) {
		const c1c2_t c = node_c1c2s[t];
		/* if the transistor connects c1 and c2... */
		if (get_transistors_on(state, c.transistor)) {
			val = addNodeToGroup(state, c.other_node, val);
		}
	}
 
    return val;
}

static inline group_value
addAllNodesToGroup(state_t *state, nodenum_t node)
{
	group_clear(state);
	return addNodeToGroup(state, node, contains_nothing);
}

static inline BOOL
getGroupValue(group_value node_value)
{
	switch (node_value) {
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

static inline void
recalcNode(state_t *state, nodenum_t node)
{
	/*
	 * get all nodes that are connected through
	 * transistors, starting with this one
	 */
	group_value node_value = addAllNodesToGroup(state, node);

	/* get the state of the group */
	BOOL newv = getGroupValue(node_value);

	/*
	 * - set all nodes to the group state
	 * - check all transistors switched by nodes of the group
	 * - collect all nodes behind toggled transistors
	 *   for the next run
	 */
	for (count_t i = 0; i < group_count(state); i++) {
		const nodenum_t nn = group_get(state, i);
		if (get_nodes_value(state, nn) != newv) {
			set_nodes_value(state, nn, newv);
            const nodenum_t startg = state->nodes_gates[nn];
            const nodenum_t endg = state->nodes_gates[nn+1];
			for (count_t t = startg; t < endg; t++) {
				transnum_t tn = state->node_block[t];
				set_transistors_on(state, tn, newv);
			}

			if (newv) {
                const nodenum_t dep_offset = state->nodes_left_dependant[nn];
                const nodenum_t dep_end = state->nodes_left_dependant[nn+1];
				for (count_t g = dep_offset; g < dep_end; g++) {
					listout_add(state, state->dependent_block[g]);
				}
			} else {
                const nodenum_t dep_offset = state->nodes_dependant[nn];
                const nodenum_t dep_end = state->nodes_dependant[nn+1];
				for (count_t g = dep_offset; g < dep_end; g++) {
					listout_add(state, state->dependent_block[g]);
				}
			}
		}
	}
}

void
recalcNodeList(state_t *state)
{
    const int max_iterations = 50;
    int j = 0;
    
	for (j = 0; j < max_iterations; j++) {	/* loop limiter */
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
        const count_t list_count = listin_count(state);
		for (count_t i = 0; i < list_count; i++) {
			nodenum_t n = listin_get(state, i);
			recalcNode(state, n);
		}
	}
    
    if (j == max_iterations) {
        fprintf(stderr,"### recalcNodeList max iterations hit, listin.count = %d\n", listin_count(state));
    }
    
    /* without this, we'll have a bogus listin on the next step */
	listout_clear(state);
}

/************************************************************
 *
 * Initialization
 *
 ************************************************************/

static inline void
add_nodes_dependant(state_t *state, nodenum_t a, nodenum_t b, nodenum_t *counts, nodenum_t offset )
{
    /* O(N^2) behavior, but only run once at initialization
    NOTE - counts are still being set and cannot be calculated from offsets
    */
    nodenum_t *dependent_data = state->dependent_block + offset;
    const nodenum_t dep_count = counts[a];
	for (count_t g = 0; g < dep_count; g++)
        if (dependent_data[g] == b)
            return;

	dependent_data[ counts[a]++ ] = b;
}

static inline void
add_nodes_left_dependant(state_t *state, nodenum_t a, nodenum_t b, nodenum_t *counts, nodenum_t offset)
{
    /* O(N^2) behavior, but only run once at initialization
    NOTE - counts are still being set and cannot be calculated from offsets
    */
    nodenum_t *dependent_data = state->dependent_block + offset;
    const nodenum_t dep_count = counts[a];
	for (count_t g = 0; g < dep_count; g++)
        if (dependent_data[g] == b)
            return;

	dependent_data[ counts[a]++ ] = b;
}


/*  6502:
        3288 transistors, 3239 used in simulation after duplicate removal
        1725 entries in node list and used in simulation
        c1c2total = 6478
        block_gate_size = 3239
        block_dep_size = 7260

    Working set = 207 KB allocations, 220 KB binary, plus system libs
                = 1.1 MB in release build
    
    down to 188K after nodes_gates
    down to 168K after dependencies and left_dependencies
    down to 145K after removing dep_counts
    down to ??? K after romoving gate_counts
    down to 122K after removing unused transistor arrays
    
    Now 122K KB allocations and 23737 steps/second
    
*/
state_t *
setupNodesAndTransistors(netlist_transdefs *transdefs, BOOL *node_is_pullup, nodenum_t nodes, nodenum_t transistors, nodenum_t vss, nodenum_t vcc)
{
	/* allocate state */
	state_t *state = malloc(sizeof(state_t));
	state->nodes = nodes;
	state->transistors = transistors;
	state->vss = vss;
	state->vcc = vcc;
    
    /* chip state - remains static during simulation */
	state->nodes_c1c2offset = calloc(state->nodes + 1, sizeof(*state->nodes_c1c2offset));
    
	state->nodes_pullup = calloc(WORDS_FOR_BITS(state->nodes), sizeof(*state->nodes_pullup));
	state->nodes_pulldown = calloc(WORDS_FOR_BITS(state->nodes), sizeof(*state->nodes_pulldown));
	state->nodes_value = calloc(WORDS_FOR_BITS(state->nodes), sizeof(*state->nodes_value));
    
	state->transistors_on = calloc(WORDS_FOR_BITS(state->transistors), sizeof(*state->transistors_on));
	state->listout_bitmap = calloc(WORDS_FOR_BITS(state->nodes), sizeof(*state->listout_bitmap));
	state->groupbitmap = calloc(WORDS_FOR_BITS(state->nodes), sizeof(*state->groupbitmap));
 
    /* group content depends on active state, not easy to predict actual size needed */
	state->group = calloc(state->nodes, sizeof(*state->group));
    
    /* ping pong state buffers */
	state->list1 = calloc(state->nodes, sizeof(*state->list1));
	state->list2 = calloc(state->nodes, sizeof(*state->list2));
	state->listin.list = state->list1;
        state->listin.count = 0;
	state->listout.list = state->list2;
        state->listout.count = 0;
    
    
    /* these are only used in initialization */
	nodenum_t *nodes_dep_count = calloc(state->nodes, sizeof(nodenum_t));
	nodenum_t *nodes_left_dep_count = calloc(state->nodes, sizeof(nodenum_t));
	count_t *nodes_gatecount = calloc(state->nodes, sizeof(count_t));
	nodenum_t *transistors_gate = calloc(state->transistors, sizeof(nodenum_t));
	nodenum_t *transistors_c1 = calloc(state->transistors, sizeof(nodenum_t));
	nodenum_t *transistors_c2 = calloc(state->transistors, sizeof(nodenum_t));

	count_t i;
    
	/* copy nodes into r/w data structure */
	for (i = 0; i < state->nodes; i++) {
		set_nodes_pullup(state, i, node_is_pullup[i]);
		nodes_gatecount[i] = 0;
	}
    
	/* Copy transistors into r/w data structure and remove duplicates */
	count_t transistors_used = 0;
	for (i = 0; i < state->transistors; i++) {
		nodenum_t gate = transdefs[i].gate;
		nodenum_t c1 = transdefs[i].c1;
		nodenum_t c2 = transdefs[i].c2;
		/* skip duplicate transistors
            O(N^2) operation, but only done once at initialization, not a significant time sink */
		BOOL found = NO;
		for (count_t j2 = 0; j2 < transistors_used; j2++) {
			if (transistors_gate[j2] == gate &&
				((transistors_c1[j2] == c1 &&
				  transistors_c2[j2] == c2) ||
				 (transistors_c1[j2] == c2 &&
				  transistors_c2[j2] == c1))) {
					 found = YES;
                     break;
				 }
		}
		if (!found) {
			transistors_gate[transistors_used] = gate;
			transistors_c1[transistors_used] = c1;
			transistors_c2[transistors_used] = c2;
			transistors_used++;
		}
	}
	state->transistors = transistors_used;


	/* cross reference transistors in nodes data structures */
	/* start by computing how many c1c2 entries should be created for each node */
	count_t *c1c2count = calloc(state->nodes, sizeof(*c1c2count));
	count_t c1c2total = 0;
	for (i = 0; i < state->transistors; i++) {
		nodenum_t gate = transistors_gate[i];
        nodenum_t c1 = transistors_c1[i];
        nodenum_t c2 = transistors_c2[i];
        
        if (gate >= nodes || c1 >= nodes || c2 >= nodes)
            fprintf(stderr,"FATAL - bad transistor definition %d\n", i);
        
		nodes_gatecount[gate]++;
		c1c2count[c1]++;
		c1c2count[c2]++;
		c1c2total += 2;
	}
    
	/* then sum the counts to find each node's offset into the c1c2 array */
	count_t c1c2offset = 0;
	for (i = 0; i < state->nodes; i++) {
		state->nodes_c1c2offset[i] = c1c2offset;
		c1c2offset += c1c2count[i];
	}
	state->nodes_c1c2offset[i] = c1c2offset;    /* fill the end entry, so we can calculate distances/counts */
    
	/* create and fill the nodes_c1c2s array according to these offsets */
	state->nodes_c1c2s = calloc(c1c2total, sizeof(*state->nodes_c1c2s));
	memset(c1c2count, 0, state->nodes * sizeof(*c1c2count));    /* zero counts so we can reuse them */
	for (i = 0; i < state->transistors; i++) {
		nodenum_t c1 = transistors_c1[i];
		nodenum_t c2 = transistors_c2[i];
		state->nodes_c1c2s[state->nodes_c1c2offset[c1] + c1c2count[c1]++] = c1c2(i, c2);
		state->nodes_c1c2s[state->nodes_c1c2offset[c2] + c1c2count[c2]++] = c1c2(i, c1);
	}
    /* this is unused after initialization */
	free(c1c2count);
    c1c2count = NULL;
    
    
    /* Sum the counts for total allocation of gates */
    size_t block_gate_size = 0;
    for (i = 0; i < state->nodes; i++) {
        block_gate_size += (size_t) nodes_gatecount[i];
    }
    
    /* Allocate the block of gate data all at once */
    nodenum_t *block_gate = calloc( block_gate_size, sizeof(*state->nodes_gates) );
    state->node_block = block_gate;
    
    /* Assign offsets from our larger block */
    state->nodes_gates = malloc((nodes+1) * sizeof(*state->nodes_gates));
    nodenum_t node_index = 0;
    for (i = 0; i < state->nodes; i++) {
        count_t count = nodes_gatecount[i];
        state->nodes_gates[i] = node_index;
        node_index += count;
    }
    state->nodes_gates[state->nodes] = node_index;
    
    /* Cross reference transistors in nodes with smaller data structures */
	memset(nodes_gatecount, 0, state->nodes * sizeof(count_t));
    for (i = 0; i < state->transistors; i++) {
        nodenum_t gate = transistors_gate[i];
        nodenum_t *gate_data = state->node_block + state->nodes_gates[gate];
        gate_data[ nodes_gatecount[gate]++ ] = i;
    }
 

    /* See how many dependent node entries we really need.
        Must happen after gatecount and nodes_gates assignments!
    */
    for (i = 0; i < state->nodes; i++) {
        nodes_dep_count[i] = 0;
        nodes_left_dep_count[i] = 0;
        nodenum_t *gate_data = state->node_block + state->nodes_gates[i];
        for (count_t g = 0; g < nodes_gatecount[i]; g++) {
            nodenum_t t = gate_data[g];
            nodenum_t c1 = transistors_c1[t];
            if (c1 != vss && c1 != vcc) {
                nodes_dep_count[i]++;
            }
            nodenum_t c2 = transistors_c2[t];
            if (c2 != vss && c2 != vcc) {
                nodes_dep_count[i]++;
            }
            nodes_left_dep_count[i]++;
        }
    }
    
	/* Sum the counts to find total size of the dependents array */
    size_t block_dep_size = 0;
    for (i = 0; i < state->nodes; i++) {
        block_dep_size += nodes_dep_count[i];
        block_dep_size += nodes_left_dep_count[i];
    }
    
    /* Allocate the dependents block all at once */
    nodenum_t *block_dep = calloc( block_dep_size, sizeof(*state->nodes_dependant) );
    state->dependent_block = block_dep;
    
    /* Assign offsets from our larger block, using only counts needed */
    state->nodes_dependant = malloc((nodes+1) * sizeof(*state->nodes_dependant));
    nodenum_t dep_index = 0;
    for (i = 0; i < state->nodes; i++) {
        nodenum_t count = nodes_dep_count[i];
        state->nodes_dependant[i] = dep_index;
        dep_index += count;
    }
    state->nodes_dependant[state->nodes] = dep_index;
    
    state->nodes_left_dependant = malloc((nodes+1) * sizeof(*state->nodes_left_dependant));
    for (i = 0; i < state->nodes; i++) {
        nodenum_t count = nodes_left_dep_count[i];
        state->nodes_left_dependant[i] = dep_index;
        dep_index += count;
    }
    state->nodes_left_dependant[state->nodes] = dep_index;
    
    /* Copy dependencies into smaller data structures */
    for (i = 0; i < state->nodes; i++) {
        nodes_dep_count[i] = 0;
        nodes_left_dep_count[i] = 0;
        nodenum_t *gate_data = state->node_block + state->nodes_gates[i];
        for (count_t g = 0; g < nodes_gatecount[i]; g++) {
            nodenum_t t = gate_data[g];
            nodenum_t c1 = transistors_c1[t];
            if (c1 != vss && c1 != vcc) {
                add_nodes_dependant(state, i, c1, nodes_dep_count, state->nodes_dependant[i]);
            }
            nodenum_t c2 = transistors_c2[t];
            if (c2 != vss && c2 != vcc) {
                add_nodes_dependant(state, i, c2, nodes_dep_count, state->nodes_dependant[i]);
            }
            if (c1 != vss && c1 != vcc) {
                add_nodes_left_dependant(state, i, c1, nodes_left_dep_count, state->nodes_left_dependant[i]);
            } else {
                add_nodes_left_dependant(state, i, c2, nodes_left_dep_count, state->nodes_left_dependant[i]);
            }
        }
    }
    
    /* these are unused after initialization */
    free(nodes_dep_count);
    nodes_dep_count = NULL;
    free(nodes_left_dep_count);
    nodes_left_dep_count = NULL;
    free(nodes_gatecount);
    nodes_gatecount = NULL;
    free(transistors_gate);
    transistors_gate = NULL;
    free(transistors_c1);
    transistors_c1 = NULL;
    free(transistors_c2);
    transistors_c2 = NULL;
    

#if 0 /* unnecessary - RESET will stabilize the network anyway */
	/* all nodes are down */
	for (nodenum_t nn = 0; nn < state->nodes; nn++) {
		set_nodes_value(state, nn, 0);
	}
	/* all transistors are off */
	for (transnum_t tn = 0; tn < state->transistors; tn++)
        set_transistors_on(state, tn, NO);
#endif

	return state;
}

void
destroyNodesAndTransistors(state_t *state)
{
    free(state->nodes_pullup);
    free(state->nodes_pulldown);
    free(state->nodes_value);
    free(state->nodes_gates);
    free(state->node_block);
    free(state->nodes_c1c2s);
    free(state->nodes_c1c2offset);
    free(state->dependent_block);
    free(state->transistors_on);
    free(state->list1);
    free(state->list2);
    free(state->listout_bitmap);
    free(state->group);
    free(state->groupbitmap);
    free(state);
}

void
stabilizeChip(state_t *state)
{
	for (count_t i = 0; i < state->nodes; i++)
        listout_add(state, i);

	recalcNodeList(state);
}

/************************************************************
 *
 * Node State
 *
 ************************************************************/

void
setNode(state_t *state, nodenum_t nn, BOOL s)
{
    set_nodes_pullup(state, nn, s);
    set_nodes_pulldown(state, nn, !s);
    listout_add(state, nn);

    recalcNodeList(state);
}

BOOL
isNodeHigh(state_t *state, nodenum_t nn)
{
	return get_nodes_value(state, nn);
}

/************************************************************
 *
 * Interfacing and Extracting State
 *
 ************************************************************/

unsigned int
readNodes(state_t *state, int count, nodenum_t *nodelist)
{
	unsigned int result = 0;
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
