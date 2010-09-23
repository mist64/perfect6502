//#define DEBUG
int verbose = 0;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef int BOOL;

typedef uint16_t nodenum_t;
typedef uint16_t transnum_t;
typedef uint16_t count_t;
typedef uint8_t state_t;

#define NO 0
#define YES 1

#define SWAPLIST(a,b) {list_t tmp = a; a = b; b = tmp; }

#include "segdefs.h"
#include "transdefs.h"
#include "nodenames.h"

#define ngnd vss
#define npwr vcc

uint8_t code[] = { 0xa9, 0x00, 0x20, 0x10, 0x00, 0x4c, 0x02, 0x00, 
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                   0xe8, 0x88, 0xe6, 0x40, 0x38, 0x69, 0x02, 0x60 };

enum {
	STATE_VCC,
	STATE_PU,
	STATE_FH,
	STATE_GND,
	STATE_FL,
	STATE_PD,
	STATE_UNDEFINED,
};

#define MAX_HIGH STATE_FH /* VCC, PU and FH are considered high */ 

#define NODES 1725
#define TRANSISTORS 3510

BOOL nodes_pullup[NODES];//XXX no idea why this array overflows!!
BOOL nodes_pulldown[NODES];
state_t nodes_state[NODES];
nodenum_t nodes_gates[NODES][NODES];
nodenum_t nodes_c1c2s[NODES][2*NODES];
count_t nodes_gatecount[NODES];
count_t nodes_c1c2count[NODES];

transnum_t transistors_name[TRANSISTORS];
nodenum_t transistors_gate[TRANSISTORS];
nodenum_t transistors_c1[TRANSISTORS];
nodenum_t transistors_c2[TRANSISTORS];

int transistors_on[TRANSISTORS/sizeof(int)+1];

void
set_transistors_on(transnum_t t, BOOL state)
{
//	/*DEBUG*/if (t>>5 > sizeof(transistors_on)) printf
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

uint8_t memory[65536];
int cycle;

uint8_t A, X, Y, S, P;
uint16_t PC;
BOOL N, Z, C;

void
setupNodesAndTransistors()
{
	count_t i;
	for (i = 0; i < sizeof(segdefs)/sizeof(*segdefs); i++) {
//printf("%d %d\n", __LINE__, i);
		nodes_pullup[i] = segdefs[i];
		nodes_state[i] = STATE_FL;
		nodes_gatecount[i] = 0;
		nodes_c1c2count[i] = 0;
	}
	for (i = 0; i < sizeof(transdefs)/sizeof(*transdefs); i++) {
		nodenum_t gate = transdefs[i].gate;
		nodenum_t c1 = transdefs[i].c1;
		nodenum_t c2 = transdefs[i].c2;
		transistors_name[i] = i;
		transistors_gate[i] = gate;
		transistors_c1[i] = c1;
		transistors_c2[i] = c2;
		nodes_gates[gate][nodes_gatecount[gate]++] = i;
		nodes_c1c2s[c1][nodes_c1c2count[c1]++] = i;
		nodes_c1c2s[c2][nodes_c1c2count[c2]++] = i;
	}
}

#ifdef DEBUG
void
printarray(nodenum_t *array, count_t count)
{
	count_t i;
	for (i = 0; i < count; i++)
		printf("%d ", array[i]);
	printf("\n");
}
#endif

nodenum_t group[NODES];
count_t groupcount;
int groupbitmap[NODES/sizeof(int)+1];

BOOL
arrayContains(nodenum_t el)
{
#if 0
	count_t i;
	for (i = 0; i < groupcount; i++) {
		if (group[i] == el) {
			return YES;
		}
	}
	return NO;
#else
	return (groupbitmap[el>>5] >> (el & 31)) & 1;
#endif
}

typedef struct {
	nodenum_t *list;
	count_t count;
	char *bitmap;
} list_t;

list_t recalc;

void
clearRecalc()
{
	bzero(recalc.bitmap, sizeof(*recalc.bitmap)*NODES);
	recalc.count = 0;
}

BOOL
recalcListContains(nodenum_t el)
{
	return recalc.bitmap[el];
}

void addNodeToGroup(nodenum_t i);

void
addNodeTransistor(nodenum_t node, transnum_t t)
{
#ifdef DEBUG
	printf("%s n=%d, t=%d, group=", __func__, node, t);
	printarray(group, groupcount);
#endif
	if (!get_transistors_on(t))
		return;
	nodenum_t other;
	if ((transistors_c1[t] != node) && (transistors_c2[t] != node)) {
		return;
	}

	if (transistors_c1[t] == node)
		other = transistors_c2[t];
	if (transistors_c2[t] == node)
		other = transistors_c1[t];
	addNodeToGroup(other);
}

void
addNodeToGroup(nodenum_t i)
{
#ifdef DEBUG
	printf("%s %d, group=", __func__, i);
	printarray(group, groupcount);
#endif
	if (arrayContains(i))
		return;
	group[groupcount++] = i;
	groupbitmap[i>>5] |= 1 << (i & 31);
	if (i == ngnd)
		return;
	if (i == npwr)
		return;
	count_t t;
	for (t = 0; t < nodes_c1c2count[i]; t++)
		addNodeTransistor(i, nodes_c1c2s[i][t]);
}

state_t
getNodeValue()
{
#ifdef DEBUG
	printf("%s group=", __func__);
	printarray(group, groupcount);
#endif
	if (arrayContains(ngnd))
		return STATE_GND;
	if (arrayContains(npwr))
		return STATE_VCC;
	state_t flstate = STATE_UNDEFINED;
	count_t i;
	for (i = 0; i < groupcount; i++) {
		nodenum_t nn = group[i];
//printf("%d %d\n", __LINE__, nn);
		if (nodes_pullup[nn])
			return STATE_PU;
		if (nodes_pulldown[nn])
			return STATE_PD;
		if ((nodes_state[nn] == STATE_FL) && (flstate == STATE_UNDEFINED))
			flstate = STATE_FL;
		if (nodes_state[nn] == STATE_FH)
			flstate = STATE_FH;
	}
	return flstate;
}

void
addRecalcNode(nodenum_t nn)
{
#ifdef DEBUG
	printf("%s nn=%d recalc.list=", __func__, nn);
	printarray(recalc.list, recalc.count);
#endif
	if (nn == ngnd || nn == npwr)
		return;
	if (recalcListContains(nn))
		return;
	recalc.list[recalc.count++] = nn;
	recalc.bitmap[nn] = 1;
}

void
floatnode(nodenum_t nn)
{
#ifdef DEBUG
	printf("%s nn=%d\n", __func__, nn);
#endif
	if (nn == ngnd || nn == npwr)
		return;
	state_t state = nodes_state[nn];
	if (state == STATE_GND || state == STATE_PD)
		nodes_state[nn] = STATE_FL;
	if (state == STATE_VCC || state == STATE_PU)
		nodes_state[nn] = STATE_FH;
#ifdef DEBUG
	printf("%s %i to state %d\n", __func__, nn, n.state);
#endif
}

BOOL
isNodeHigh(nodenum_t nn)
{
#ifdef DEBUG
	printf("%s nn=%d state=%d\n", __func__, nn, nodes[nn].state);
	printf("%s nn=%d res=%d\n", __func__, nn, nodes[nn].state <= MAX_HIGH);
#endif
	return nodes_state[nn] <= MAX_HIGH;
}

void
recalcTransistor(transnum_t tn)
{
#ifdef DEBUG
	printf("%s tn=%d, recalc.list=", __func__, tn);
	printarray(recalc.list, recalc.count);
#endif
	BOOL on = isNodeHigh(transistors_gate[tn]);
	if (on == get_transistors_on(tn))
		return;
	set_transistors_on(tn, on);
	if (!on) {
		floatnode(transistors_c1[tn]);
		floatnode(transistors_c2[tn]);
	}
	addRecalcNode(transistors_c1[tn]);
	addRecalcNode(transistors_c2[tn]);
}

void
recalcNode(nodenum_t node)
{
#ifdef DEBUG
	printf("%s node=%d, recalc.list=", __func__, node);
	printarray(recalc.list, recalc.count);
#endif
	if (node == ngnd || node == npwr)
		return;

	groupcount = 0;
	bzero(groupbitmap, sizeof(groupbitmap));
	addNodeToGroup(node);

	state_t newv = getNodeValue();
	count_t i;
	for (i = 0; i < groupcount; i++) {
		nodes_state[group[i]] = newv;
		count_t t;
		for (t = 0; t < nodes_gatecount[group[i]]; t++)
			recalcTransistor(nodes_gates[group[i]][t]);
	}
}

void
recalcNodeList(nodenum_t *list, count_t count)
{
#ifdef DEBUG
	printf("%s list=", __func__);
	printarray(list, count);
#endif
	nodenum_t list1[NODES];
	char bitmap1[NODES];
	char bitmap2[NODES];

	list_t current;
	current.list = list;
	current.count = count;
	current.bitmap = bitmap2;

	recalc.list = list1;
	recalc.bitmap = bitmap1;

	count_t i;
	int j;
	for (j = 0; j < 100; j++) {	// loop limiter
		clearRecalc();

		if (!current.count)
			return;
#ifdef DEBUG
		printf("%s iteration=%d, current.list=", __func__, j);
		printarray(current.list, current.count);
#endif
		for (i = 0; i < current.count; i++)
			recalcNode(current.list[i]);

		SWAPLIST(current, recalc);

	}
}

void
recalcAllNodes()
{
#ifdef DEBUG
	printf("%s\n", __func__);
#endif
	printf("%s count=%d\n", __func__, NODES);
	nodenum_t list[NODES];
	count_t i;
	for (i = 0; i < NODES; i++)
		list[i] = i;
	recalcNodeList(list, NODES);
}

void
setLow(nodenum_t nn)
{
#ifdef DEBUG
	printf("%s nn=%d\n", __func__, nn);
#endif
//printf("%d %d\n", __LINE__, nn);
	nodes_pullup[nn] = NO;
	nodes_pulldown[nn] = YES;
	nodenum_t list[NODES];
	list[0] = nn;
	recalcNodeList(list, 1);
}

void
setHigh(nodenum_t nn)
{
#ifdef DEBUG
	printf("%s nn=%d\n", __func__, nn);
#endif
//printf("%d %d\n", __LINE__, nn);
	nodes_pullup[nn] = YES;
	nodes_pulldown[nn] = NO;
	nodenum_t list[NODES];
	list[0] = nn;
	recalcNodeList(list, 1);
}

void
setHighLow(nodenum_t nn, BOOL val)
{
	if (val)
		setHigh(nn);
	else
		setLow(nn);
}

void
writeDataBus(uint8_t x)
{
	nodenum_t recalcs[NODES];
	count_t recalcscount = 0;
	int i;
	for (i = 0; i < 8; i++) {
		nodenum_t nn;
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
			nodes_pulldown[nn] = YES;
//printf("%d %d\n", __LINE__, nn);
			nodes_pullup[nn] = NO;
		} else {
			nodes_pulldown[nn] = NO;
//printf("%d %d\n", __LINE__, nn);
			nodes_pullup[nn] = YES;
		}
		recalcs[recalcscount++] = nn;
		x >>= 1;
	}
	recalcNodeList(recalcs, recalcscount);
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
	if (verbose)
		printf("POKE $%04X, $%02X\n", a, d);
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

void
setA(uint8_t val)
{
	setHighLow(a0, (val >> 0) & 1);
	setHighLow(a1, (val >> 1) & 1);
	setHighLow(a2, (val >> 2) & 1);
	setHighLow(a3, (val >> 3) & 1);
	setHighLow(a4, (val >> 4) & 1);
	setHighLow(a5, (val >> 5) & 1);
	setHighLow(a6, (val >> 6) & 1);
	setHighLow(a7, (val >> 7) & 1);	
}

void
setX(uint8_t val)
{
	setHighLow(x0, (val >> 0) & 1);
	setHighLow(x1, (val >> 1) & 1);
	setHighLow(x2, (val >> 2) & 1);
	setHighLow(x3, (val >> 3) & 1);
	setHighLow(x4, (val >> 4) & 1);
	setHighLow(x5, (val >> 5) & 1);
	setHighLow(x6, (val >> 6) & 1);
	setHighLow(x7, (val >> 7) & 1);	
}

void
setY(uint8_t val)
{
	setHighLow(y0, (val >> 0) & 1);
	setHighLow(y1, (val >> 1) & 1);
	setHighLow(y2, (val >> 2) & 1);
	setHighLow(y3, (val >> 3) & 1);
	setHighLow(y4, (val >> 4) & 1);
	setHighLow(y5, (val >> 5) & 1);
	setHighLow(y6, (val >> 6) & 1);
	setHighLow(y7, (val >> 7) & 1);	
}

void
setSP(uint8_t val)
{
	setHighLow(s0, (val >> 0) & 1);
	setHighLow(s1, (val >> 1) & 1);
	setHighLow(s2, (val >> 2) & 1);
	setHighLow(s3, (val >> 3) & 1);
	setHighLow(s4, (val >> 4) & 1);
	setHighLow(s5, (val >> 5) & 1);
	setHighLow(s6, (val >> 6) & 1);
	setHighLow(s7, (val >> 7) & 1);	
}

void
setP(uint8_t val)
{
	setHighLow(p0, (val >> 0) & 1);
	setHighLow(p1, (val >> 1) & 1);
	setHighLow(p2, (val >> 2) & 1);
	setHighLow(p3, (val >> 3) & 1);
	setHighLow(p4, (val >> 4) & 1);
	setHighLow(p5, (val >> 5) & 1);
	setHighLow(p6, (val >> 6) & 1);
	setHighLow(p7, (val >> 7) & 1);	
}

void
setNOTIR(uint8_t val)
{
	setHighLow(notir0, (val >> 0) & 1);
	setHighLow(notir1, (val >> 1) & 1);
	setHighLow(notir2, (val >> 2) & 1);
	setHighLow(notir3, (val >> 3) & 1);
	setHighLow(notir4, (val >> 4) & 1);
	setHighLow(notir5, (val >> 5) & 1);
	setHighLow(notir6, (val >> 6) & 1);
	setHighLow(notir7, (val >> 7) & 1);	
}

void
setPCL(uint8_t val)
{
	setHighLow(pcl0, (val >> 0) & 1);
	setHighLow(pcl1, (val >> 1) & 1);
	setHighLow(pcl2, (val >> 2) & 1);
	setHighLow(pcl3, (val >> 3) & 1);
	setHighLow(pcl4, (val >> 4) & 1);
	setHighLow(pcl5, (val >> 5) & 1);
	setHighLow(pcl6, (val >> 6) & 1);
	setHighLow(pcl7, (val >> 7) & 1);	
}

void
setPCH(uint8_t val)
{
	setHighLow(pch0, (val >> 0) & 1);
	setHighLow(pch1, (val >> 1) & 1);
	setHighLow(pch2, (val >> 2) & 1);
	setHighLow(pch3, (val >> 3) & 1);
	setHighLow(pch4, (val >> 4) & 1);
	setHighLow(pch5, (val >> 5) & 1);
	setHighLow(pch6, (val >> 6) & 1);
	setHighLow(pch7, (val >> 7) & 1);	
}

void
setPC(uint16_t val)
{
	setPCL(val & 0xFF);
	setPCH(val >> 8);
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
	nodenum_t nn;
	for (nn = 0; nn < NODES; nn++)
		nodes_state[nn] = STATE_FL;
	nodes_state[ngnd] = STATE_GND;
	nodes_state[npwr] = STATE_VCC;
	transnum_t tn;
	for (tn = 0; tn < TRANSISTORS; tn++) 
		set_transistors_on(tn, NO);
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
		string += ' '+transistors_on[i];
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
	if (verbose)
		chipStatus();

	PC = readPC();
	if (PC >= 0xFF90 && ((PC - 0xFF90) % 3 == 0) && isNodeHigh(clk0)) {
		A = readA();
		X = readX();
		Y = readY();
		S = readSP();
		P = readP();
		N = P >> 7;
		Z = (P >> 1) & 1;
		C = P & 1;

#if 1
		kernal_dispatch();

		/*
		LDA #P
		PHA
		LDA #A
		LDX #X
		LDY #Y
		RTI
		*/
		memory[0xf800] = 0xA9;
		memory[0xf801] = P;
		memory[0xf802] = 0x48;
		memory[0xf803] = 0xA9;
		memory[0xf804] = A;
		memory[0xf805] = 0xA2;
		memory[0xf806] = X;
		memory[0xf807] = 0xA0;
		memory[0xf808] = Y;
		memory[0xf809] = 0x40;

#if 0
		PC = memory[0x0100 + S+1] | memory[0x0100 + S + 2] << 8;
		PC++;
		S += 2;
		P &= 0x7C; // clear N, Z, C
		P |= (N << 7) | (Z << 1) | C;
		
		setA(A);
		setX(X);
		setY(Y);
//		setP(P);
//		recalcAllNodes(); 
#endif
#endif
	}
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
//	memset(memory + 0xFF90, 0x60, 0x70);
	int addr;
	for (addr = 0xFF90; addr < 0x10000; addr += 3) {
		memory[addr+0] = 0x4C;
		memory[addr+1] = 0x00;
		memory[addr+2] = 0xF8;
	}
#if 0
	memory[0xfffc] = 0x94;
	memory[0xfffd] = 0xE3;
#else
	memory[0xf000] = 0x20;
	memory[0xf001] = 0x94;
	memory[0xf002] = 0xE3;
	
	memory[0xfffc] = 0x00;
	memory[0xfffd] = 0xF0;
#endif
#endif
	steps();
}

void
setup()
{
	setupNodesAndTransistors();
	initChip();
	go();
}

int
main()
{
	setup();
	return 0;
}
