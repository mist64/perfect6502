//#define DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
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

#define NODES 1725
#define TRANSISTORS 3510

typedef struct {
	BOOL pullup;
	BOOL pulldown;
	int state;
	int gates[NODES];
	int c1c2s[2*NODES];
	int gatecount;
	int c1c2count;
} node_t;

node_t nodes[NODES];

#define EMPTY -1

typedef struct {
	int name;
	BOOL on;
	int gate;
	int c1;
	int c2;
} transistor_t;

transistor_t transistors[TRANSISTORS];

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
//		printf("1 gate=%d, gatecount=%d\n", gate, nodes[gate].gatecount);
		nodes[gate].gates[nodes[gate].gatecount++] = i;
//		printf("2 gate=%d, gatecount=%d\n", gate, nodes[gate].gatecount);
		if (nodes[gate].gatecount > NODES)
			printf("0BIG\n");
		nodes[c1].c1c2s[nodes[c1].c1c2count++] = i;
		nodes[c2].c1c2s[nodes[c2].c1c2count++] = i;
//		printf("3 gate=%d, c1c2count=%d\n", gate, nodes[gate].c1c2count);
		if (nodes[gate].c1c2count > 2*NODES)
			printf("1BIG\n");
	}
}

#ifdef DEBUG
void
printarray(int *array, int count)
{
	int i;
	for (i = 0; i < count; i++)
		printf("%d ", array[i]);
	printf("\n");
}
#endif

BOOL
arrayContains(int *arr, int count, int el)
{
#ifdef DEBUG
	printf("%s el=%d, arr=", __func__, el);
	printarray(arr, count);
#endif
	int i;
	for (i = 0; i < count; i++) {
		if (arr[i] == el) {
#ifdef DEBUG
			printf("YES\n");
#endif
			return YES;
		}
	}
#ifdef DEBUG
	printf("NO\n");
#endif
	return NO;
}

void addNodeToGroup(int i, int *group, int *groupcount);

void
addNodeTransistor(int node, int t, int *group, int *groupcount)
{
#ifdef DEBUG
	printf("%s n=%d, t=%d, group=", __func__, node, t);
	printarray(group, *groupcount);
#endif
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
#ifdef DEBUG
	printf("%s %d, group=", __func__, i);
	printarray(group, *groupcount);
#endif
	if (arrayContains(group, *groupcount, i))
		return;
	group[*groupcount] = i;
	(*groupcount)++;
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
#ifdef DEBUG
	printf("%s group=", __func__);
	printarray(group, groupcount);
#endif
	if (arrayContains(group, groupcount, ngnd))
		return STATE_GND;
	if (arrayContains(group, groupcount, npwr))
		return STATE_VCC;
	int flstate = STATE_UNDEFINED;
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
addRecalcNode(int nn, int *recalclist, int *recalccount)
{
#ifdef DEBUG
	printf("%s nn=%d recalclist=", __func__, nn);
	printarray(recalclist, *recalccount);
#endif
	if (nn == ngnd)
		return;
	if (nn == npwr)
		return;
	if (arrayContains(recalclist, *recalccount, nn))
		return;
	recalclist[*recalccount] = nn;
	(*recalccount)++;
}

void
floatnode(int nn)
{
#ifdef DEBUG
	printf("%s nn=%d\n", __func__, nn);
#endif
	if (nn == ngnd)
		return;
	if (nn == npwr)
		return;
	node_t n = nodes[nn];
	if (n.state == STATE_GND)
		nodes[nn].state = STATE_FL;
	if (n.state == STATE_PD)
		nodes[nn].state = STATE_FL;
	if (n.state == STATE_VCC)
		nodes[nn].state = STATE_FH;
	if (n.state == STATE_PU)
		nodes[nn].state = STATE_FH;
#ifdef DEBUG
	printf("%s %i to state %d\n", __func__, nn, n.state);
#endif
}

void
turnTransistorOn(transistor_t *t, int *recalclist, int *recalccount)
{
	if (t->on)
		return;
#ifdef DEBUG
	printf("%s t%d, %d, %d, %d\n", __func__, t->name, t->gate, t->c1, t->c2);
#endif
	t->on = YES;
	addRecalcNode(t->c1, recalclist, recalccount);
	addRecalcNode(t->c2, recalclist, recalccount);
}

void
turnTransistorOff(transistor_t *t, int *recalclist, int *recalccount)
{
	if (!t->on)
		return;
#ifdef DEBUG
	printf("%s t%d, %d, %d, %d\n", __func__, t->name, t->gate, t->c1, t->c2);
#endif
	t->on = NO;
	floatnode(t->c1);
	floatnode(t->c2);
	addRecalcNode(t->c1, recalclist, recalccount);
	addRecalcNode(t->c2, recalclist, recalccount);
}

BOOL
isNodeHigh(int nn)
{
#ifdef DEBUG
	printf("%s nn=%d state=%d\n", __func__, nn, nodes[nn].state);
	printf("%s nn=%d res=%d\n", __func__, nn, (nodes[nn].state == STATE_VCC) ||
            (nodes[nn].state == STATE_PU) ||
            (nodes[nn].state == STATE_FH));
#endif
//printf("%s nn=%d res=%d\n", __func__, nn, nodes[nn].state);
	return ((nodes[nn].state == STATE_VCC) ||
            (nodes[nn].state == STATE_PU) ||
            (nodes[nn].state == STATE_FH));
}

void
recalcTransistor(int tn, int *recalclist, int *recalccount)
{
#ifdef DEBUG
	printf("%s tn=%d, recalclist=", __func__, tn);
	printarray(recalclist, *recalccount);
#endif
	transistor_t *t = &transistors[tn];
	if (isNodeHigh(t->gate))
		turnTransistorOn(t, recalclist, recalccount);
	else
		turnTransistorOff(t, recalclist, recalccount);
}

void
recalcNode(int node, int *recalclist, int *recalccount)
{
#ifdef DEBUG
	printf("%s node=%d, recalclist=", __func__, node);
	printarray(recalclist, *recalccount);
#endif
	if (node == ngnd)
		return;
	if (node == npwr)
		return;

	int group[2000];
	int groupcount = 0;
	addNodeToGroup(node, group, &groupcount);

	int newv = getNodeValue(group, groupcount);
	int i;
#ifdef DEBUG
	printf("%s %i, group=", __func__, node);
	printarray(group, groupcount);
#endif
	for (i = 0; i < groupcount; i++) {
//printf("i=%d\n", i);
//printf("group[i]=%d\n", group[i]);
		node_t n = nodes[group[i]];
#ifdef DEBUG
		if (n.state != newv)
			printf("%s %d, states %d,%d\n", __func__, group[i], n.state, newv);
#endif
		nodes[group[i]].state = newv;
		int t;
#ifdef DEBUG
		printf("loop x %d\n", n.gatecount);
#endif
//printf("there are %d gates\n", n.gatecount);
		for (t = 0; t < n.gatecount; t++)
			recalcTransistor(n.gates[t], recalclist, recalccount);
	}
}

void
recalcNodeList(int *list, int count)
{
#ifdef DEBUG
	printf("%s list=", __func__);
	printarray(list, count);
#endif
	int recalclist[2000];
	int recalccount = 0;
	int i, j;
	for (j = 0; j < 100; j++) {	// loop limiter
		if (!count)
			return;
#ifdef DEBUG
		printf("%s iteration=%d, list=", __func__, j);
		printarray(list, count);
#endif
//printf("%s:%d iteration=%d count=%d\n", __func__, __LINE__, j, count);
//printf("before: %d\n", recalccount);
		for (i = 0; i < count; i++)
			recalcNode(list[i], recalclist, &recalccount);
//printf("%s:%d iteration=%d recalccount=%d\n", __func__, __LINE__, j, recalccount);
//printf("after: %d\n", recalccount);
		for (i = 0; i < recalccount; i++)
			list[i] = recalclist[i];
//printf("%s:%d iteration=%d\n", __func__, __LINE__, j);
		count = recalccount;
		recalccount = 0;
	}
}

void
recalcAllNodes()
{
#ifdef DEBUG
	printf("%s\n", __func__);
#endif
	printf("%s count=%d\n", __func__, NODES);
	int list[NODES];
	int i;
	for (i = 0; i < NODES; i++)
		list[i] = i;
	recalcNodeList(list, NODES);
}

void
setLow(int nn)
{
#ifdef DEBUG
	printf("%s nn=%d\n", __func__, nn);
#endif
	nodes[nn].pullup = NO;
	nodes[nn].pulldown = YES;
	int list[NODES];
	list[0] = nn;
	recalcNodeList(list, 1);
}

void
setHigh(int nn)
{
#ifdef DEBUG
	printf("%s nn=%d\n", __func__, nn);
#endif
	nodes[nn].pullup = YES;
	nodes[nn].pulldown = NO;
	int list[NODES];
	list[0] = nn;
	recalcNodeList(list, 1);
}

void
writeDataBus(uint8_t x)
{
	int recalcs[NODES];
	int recalcscount = 0;
	int i;
	for (i = 0; i < 8; i++) {
		int nn;
		switch (i) {
		case 0: nn = db0; break;
		case 1: nn = db1; break;
		case 2: nn = db2; break;
		case 3: nn = db3; break;
		case 4: nn = db4; break;
		case 5: nn = db5; break;
		case 6: nn = db6; break;
		case 7: nn = db7; break;
		}
		if ((x & 1) == 0) {
			nodes[nn].pulldown = YES;
			nodes[nn].pullup = NO;
		} else {
			nodes[nn].pulldown = NO;
			nodes[nn].pullup = YES;
		}
		recalcs[recalcscount++] = nn;
		x >>= 1;
	}
	recalcNodeList(recalcs, recalcscount);
}

uint8_t mRead(uint16_t a)
{
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

void
handleBusRead()
{
	if (isNodeHigh(rw))
		writeDataBus(mRead(readAddressBus()));
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
	printf("POKE $0x%04X, $%02X\n", a, d);
	memory[a] = d;
}

void
handleBusWrite()
{
	if (!isNodeHigh(rw)) {
		uint16_t a = readAddressBus();
		uint8_t d = readDataBus();
		mWrite(a,d);
	}
}

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
	printf("halfcyc:%d phi0:%d AB:%04X D:%02X RnW:%d PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X\n",
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
			readP());
#if 0
	var machine1 =
	        ' halfcyc:' + cycle +
	        ' phi0:' + readBit('clk0') +
                ' AB:' + hexWord(ab) +
	        ' D:' + hexByte(readDataBus()) +
	        ' RnW:' + readBit('rw');
	var machine2 =
	        ' PC:' + hexWord(readPC()) +
	        ' A:' + hexByte(readA()) +
	        ' X:' + hexByte(readX()) +
	        ' Y:' + hexByte(readY()) +
	        ' SP:' + hexByte(readSP()) +
	        ' ' + readPstring();
	var machine3 =
	        ' Sync:' + readBit('sync')
		' IRQ:' + readBit('irq') +
	        ' NMI:' + readBit('nmi');
	var machine4 =
	        ' IR:' + hexByte(255 - readBits('notir', 8)) +
	        ' idl:' + hexByte(255 - readBits('idl', 8)) +
	        ' alu:' + hexByte(255 - readBits('alu', 8)) +
	        ' TCstate:' + readBit('clock1') + readBit('clock2') +
                	readBit('t2') + readBit('t3') + readBit('t4') + readBit('t5');
        var machine5 =
                ' notRdy0:' + readBit('notRdy0') +
                ' fetch:'   + readBit('fetch') +
                ' clearIR:' + readBit('clearIR') +
                ' D1x1:'    + readBit('D1x1');
        setStatus(machine1 + "<br>" + machine2);
	if (loglevel>2 && ctrace) {
		console.log(machine1 + " " + machine2 + " " + machine3 + " " + machine4 + " " + machine5);
	}
#endif
}

void
halfStep()
{
#ifdef DEBUG
	printf("%s\n", __func__);
#endif
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
#ifdef DEBUG
	printf("%s\n", __func__);
#endif
	int nn;
	for (nn = 0; nn < NODES; nn++)
		nodes[nn].state = STATE_FL;
	nodes[ngnd].state = STATE_GND;
	nodes[npwr].state = STATE_VCC;
	int tn;
	for (tn = 0; tn < TRANSISTORS; tn++) 
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

#if 1
	int i;
	for (i = 0; i < 8; i++) {
		setHigh(clk0);
		setLow(clk0);
	}
#endif
	setHigh(res);
#if 0
	for (i = 0; i < 18; i++)
		halfStep();
#endif
	cycle = 0;
//	chipStatus();
}

void
step()
{
#ifdef DEBUG
	printf("%s\n", __func__);
#endif
	halfStep();
	cycle++;
	chipStatus();
}

void
steps()
{
#ifdef DEBUG
	printf("%s\n", __func__);
#endif
	for (;;)
		step();
}

void
go(n)
{
#ifdef DEBUG
	printf("%s\n", __func__);
#endif

#if 0
	memcpy(memory, code, sizeof(code));
	memory[0xfffc] = 0x00;
	memory[0xfffd] = 0x00;
#else
	FILE *f;
	f = fopen("cbmbasic.bin", "r");
	fread(memory + 0xA000, 1, 17591, f);
	fclose(f);
	memory[0xfffc] = 0x94;
	memory[0xfffd] = 0xE3;
#endif
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
