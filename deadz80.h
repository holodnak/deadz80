#ifndef __deadz80_h__
#define __deadz80_h__

#define Z80_NUMPAGES		16
#define Z80_PAGE_SHIFT	12
#define Z80_PAGE_MASK	0xFFF

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef u8 (*irqfunc_t)();
typedef u8 (*readfunc_t)(u32);
typedef void (*writefunc_t)(u32,u8);

typedef struct z80regs_s {
        union {
                struct {u8 f, a;} b;
                u16 w;
        } af;
        union {
                struct {u8 c, b;} b;
                u16 w;
        } bc;
        union {
                struct {u8 e, d;} b;
                u16 w;
        } de;
        union {
                struct {u8 l, h;} b;
                u16 w;
        } hl;
} z80regs_t;

typedef struct deadz80_s {
	z80regs_t	main, alt;				//register sets
	z80regs_t	*regs;					//pointer to active register set
	u16			pc, sp;					//program counter, stack pointer
	u8				i, r;

	union {
		struct {
			u8 l, h;
		} b;
		u16 w;
	} ix, iy;								//index registers

	u8				iff1,iff2;
	u8				imfa,imfb;

	u32			cycles;					//cycle counter

	u8				*readpages[Z80_NUMPAGES];
	u8				*writepages[Z80_NUMPAGES];
	readfunc_t	readfuncs[Z80_NUMPAGES], ioreadfunc;
	writefunc_t	writefuncs[Z80_NUMPAGES], iowritefunc;
	irqfunc_t	irqfunc;

	u8				opcode, opcode2;		//opcodes currently being executed
	u8				nmistate, irqstate;	//states of the nmi/irq lines
	u8				halt;						//cpu is halted indicator
	u8				intmode, insideirq;

} deadz80_t;

void deadz80_init();
void deadz80_setcontext(deadz80_t *z);
deadz80_t *deadz80_getcontext();
void deadz80_reset();
void deadz80_step();
u32 deadz80_disassemble(char *dest, u32 p);

#endif
