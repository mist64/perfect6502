#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uint8_t;
typedef int BOOL;

#define NO 0
#define YES 1

#include "segdefs.h"
#include "transdefs.h"
#include "nodenames.h"

#define ngnd vss
#define npwr vcc

uint8_t code[] = { 0xa9, 0x00, 0x20, 0x10, 0x00, 0x4c, 0x02, 0x00, 
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                   0xe8, 0x88, 0xe6, 0x40, 0x38, 0x69, 0x02, 0x60 };

enum {
	STATE_UNDEFINED,
	STATE_GND,
	STATE_VCC,
	STATE_FL,
	STATE_FH,
	STATE_PD,
	STATE_PU
};

typedef struct {
	BOOL pullup;
	BOOL pulldown;
	int state;
	int gates[50];
	int c1c2s[50];
	int gatecount;
	int c1c2count;
} node_t;

node_t nodes[1725];

#define EMPTY -1

struct {
	int name;
	BOOL on;
	int gate;
	int c1;
	int c2;
} transistors[3510];

uint8_t memory[65536];
int cycle;

void
setupNodes()
{
	int i;
	for (i = 0; i < sizeof(segdefs)/sizeof(*segdefs); i++) {
		nodes[i].pullup = segdefs[i];
		nodes[i].state = STATE_FL;
		nodes[i].gatecount = 0;
		nodes[i].c1c2count = 0;
	}
}

void
setupTransistors()
{
	int i;
	for (i = 0; i < sizeof(transdefs)/sizeof(*transdefs); i++) {
		int gate = transdefs[i].gate;
		int c1 = transdefs[i].c1;
		int c2 = transdefs[i].c2;
		transistors[i].name = i;
		transistors[i].on = NO;
		transistors[i].gate = gate;
		transistors[i].c1 = c1;
		transistors[i].c2 = c2;
		nodes[gate].gates[nodes[gate].gatecount++] = i;
		nodes[c1].c1c2s[nodes[c1].c1c2count++] = i;
		nodes[c2].c1c2s[nodes[c2].c1c2count++] = i;
	}
}

BOOL
arrayContains(int *arr, int count, int el)
{
	int i;
	for (i = 0; i < count; i++) {
		if (arr[i] == el)
			return YES;
	}
	return NO;
}

void addNodeToGroup(int i, int *group, int *groupcount);

void
addNodeTransistor(int node, int t, int *group, int *groupcount)
{
	if (!transistors[t].on)
		return;
	int other;
	if (transistors[t].c1 == node)
		other = transistors[t].c2;
	if (transistors[t].c2 == node)
		other = transistors[t].c1;
	addNodeToGroup(other, group, groupcount);
}

void
addNodeToGroup(int i, int *group, int *groupcount)
{
	if (arrayContains(group, *groupcount, i))
		return;
	group[*groupcount++] = i;
	if (i == ngnd)
		return;
	if (i == npwr)
		return;
	int t;
	for (t = 0; t < nodes[i].c1c2count; t++)
		addNodeTransistor(i, nodes[i].c1c2s[t], group, groupcount);
}

int
getNodeValue(int *group, int groupcount)
{
	if (arrayContains(group, groupcount, ngnd))
		return STATE_GND;
	if (arrayContains(group, groupcount, npwr))
		return STATE_VCC;
	int flstate;
	int i;
	for (i = 0; i < groupcount; i++) {
		int nn = group[i];
		node_t n = nodes[nn];
		if (n.pullup)
			return STATE_PU;
		if (n.pulldown)
			return STATE_PD;
		if ((n.state == STATE_FL) && (flstate == STATE_UNDEFINED))
			flstate = STATE_FL;
		if (n.state== STATE_FH)
			flstate = STATE_FH;
	}
	return flstate;
}

void
recalcNode(int node, int *recalclist, int *recalccount)
{
	if (node == ngnd)
		return;
	if (node == npwr)
		return;

	int *group = malloc(1000);
	int groupcount = 0;
	addNodeToGroup(node, group, &groupcount);


#if 0 
	var newv = getNodeValue(group);
	if(ctrace) console.log('recalc', node, group);
	for(var i in group){
		var n = nodes[group[i]];
		if(n.state!=newv && ctrace) console.log(group[i], n.state, newv);
		n.state = newv;
		for(var t in n.gates) recalcTransistor(n.gates[t], recalclist);
	}
#endif
}

void
recalcNodeList(int *list, int count)
{
	int recalclist[1000];
	int recalccount = 0;
	int i, j;
	for (j = 0; j < 100; j++) {	// loop limiter
		if (!count)
			return;
		for (i = 0; i < count; i++)
			recalcNode(list[i], recalclist, &recalccount);
		for (i = 0; i < recalccount; i++)
			list[i] = recalclist[i];
		count = recalccount;
		recalccount = 0;
	}
}

void
recalcListOfOne(int nn)
{
	printf("TODO %s\n", __func__);
}

void
recalcAllNodes()
{
	printf("TODO %s\n", __func__);
}

void
setLow(int nn)
{
	nodes[nn].pullup = NO;
	nodes[nn].pulldown = YES;
	recalcListOfOne(nn);
}

void
setHigh(int nn)
{
	nodes[nn].pullup = YES;
	nodes[nn].pulldown = NO;
	recalcListOfOne(nn);
}

BOOL
isNodeHigh(int node)
{
	return NO;
}

void
handleBusRead()
{
	printf("TODO %s\n", __func__);
}

void
handleBusWrite()
{
	printf("TODO %s\n", __func__);
}

void
chipStatus()
{

}

void
halfStep()
{
	if (isNodeHigh(clk0)) {
		setLow(clk0);
		handleBusRead();
	} else {
		setHigh(clk0);
		handleBusWrite();
	}
}

void
initChip()
{
	int nn;
	for (nn = 0; nn < sizeof(nodes)/sizeof(*nodes); nn++)
		nodes[nn].state = STATE_FL;
	nodes[ngnd].state = STATE_GND;
	nodes[npwr].state = STATE_VCC;
	int tn;
	for (tn = 0; tn < sizeof(transistors)/sizeof(*transistors); tn++) 
		transistors[tn].on = NO;
	setLow(res);
	setLow(clk0);
	setHigh(rdy);
	setLow(so);
	setHigh(irq);
	setHigh(nmi);
	recalcAllNodes(); 

#if 0
	var string = '';
	for (var i in nodes) {
		string += ' '+nodes[i].pullup;
	}
	console.log(string);

	string = '';
	for (var i in transistors) {
		string += ' '+transistors[i].on;
	}
	console.log(string);
#endif

	int i;
	for (i = 0; i < 8; i++) {
		setHigh(clk0);
		setLow(clk0);
	}
	setHigh(res);
	for (i = 0; i < 18; i++)
		halfStep();
	cycle = 0;
//	chipStatus();
}

void
step()
{
	printf("%s\n", __func__);
	halfStep();
	cycle++;
	chipStatus();
}

void
steps()
{
	for (;;)
		step();
}

void
go(n)
{
	memcpy(memory, code, sizeof(code));
	code[0xfffc] = 0x00;
	code[0xfffd] = 0x00;
	steps();
}

void
setup()
{
	setupNodes();
	setupTransistors();
	initChip();
	go();
}

int
main()
{
	setup();
	return 0;
}
