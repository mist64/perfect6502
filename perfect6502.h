extern void initAndResetChip();
extern void resetChip();
extern void step();
extern void chipStatus();
extern unsigned short readPC();
extern unsigned char readA();
extern unsigned char readX();
extern unsigned char readY();
extern unsigned char readSP();
extern unsigned char readP();
extern unsigned int readRW();
extern unsigned short readAddressBus();
extern unsigned char readDataBus();
extern unsigned char readIR();

extern unsigned char memory[65536];
extern unsigned int cycle;

