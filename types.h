#ifndef _TYPES_H_
#define _TYPES_H_

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned char BOOL;
typedef uint16_t nodenum_t;

typedef struct {
	nodenum_t gate;
	nodenum_t c1;
	nodenum_t c2;
} netlist_transdefs;

#define YES 1
#define NO 0

#endif
