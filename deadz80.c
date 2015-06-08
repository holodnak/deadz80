#include <stdio.h>
#include <string.h>
#include "deadz80.h"

static deadz80_t internalz80;					//default z80 context
static deadz80_t *z80;							//pointer to active z80 context

#define PC			z80->pc
#define SP			z80->sp

#define A			z80->regs->af.b.a
#define F			z80->regs->af.b.f
#define B			z80->regs->bc.b.b
#define C			z80->regs->bc.b.c
#define D			z80->regs->de.b.d
#define E			z80->regs->de.b.e
#define H			z80->regs->hl.b.h
#define L			z80->regs->hl.b.l
#define AF			z80->regs->af.w
#define BC			z80->regs->bc.w
#define DE			z80->regs->de.w
#define HL			z80->regs->hl.w
#define IX			z80->ix.w
#define IXL			z80->ix.b.l
#define IXH			z80->ix.b.h
#define IY			z80->iy.w
#define IYL			z80->iy.b.l
#define IYH			z80->iy.b.h

#define IFF1		z80->iff1
#define IFF2		z80->iff2
#define HALT		z80->halt
#define OPCODE		z80->opcode
#define CYCLES		z80->cycles
#define INTMODE	z80->intmode
#define INSIDEIRQ	z80->insideirq

#define NMISTATE	z80->nmistate
#define IRQSTATE	z80->irqstate

#define FLAG_C	0x01
#define FLAG_N	0x02
#define FLAG_P	0x04
#define FLAG_V	FLAG_P
#define FLAG_X	0x08
#define FLAG_H	0x10
#define FLAG_Y	0x20
#define FLAG_Z	0x40
#define FLAG_S	0x80

//reading/writing helper functions
#define read8 deadz80_memread
#define write8 deadz80_memwrite

//table for parity flag calculation
unsigned char parity[256] = {
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
};

void deadz80_init()
{
	z80 = &internalz80;		//setup cpu context
	z80->regs = &z80->main;
	memset(z80,0,sizeof(deadz80_t));
}

void deadz80_setcontext(deadz80_t *z)
{
	z80 = z;
}

deadz80_t *deadz80_getcontext()
{
	return(z80);
}

void deadz80_set_nmi(u8 state)
{
	NMISTATE |= state;
}

void deadz80_clear_nmi(u8 state)
{
	NMISTATE &= ~state;
}

void deadz80_set_irq(u8 state)
{
	IRQSTATE |= state;
}

void deadz80_clear_irq(u8 state)
{
	IRQSTATE &= ~state;
}

//not public functions
__inline u8 deadz80_memread(u32 addr)
{
	int num = addr >> Z80_PAGE_SHIFT;
	u8 *page = z80->readpages[num];

	if (page) {
		return(page[addr & Z80_PAGE_MASK]);
	}
	else if (z80->readfuncs[num]) {
		return(z80->readfuncs[num](addr));
	}
	printf("unhandled read $%04X\n", addr);
	return(0);
}

__inline void deadz80_memwrite(u32 addr, u8 data)
{
	int num = addr >> Z80_PAGE_SHIFT;
	u8 *page = z80->writepages[num];

	if(page) {
		page[addr & Z80_PAGE_MASK] = data;
	}
	else if (z80->writefuncs[num]){
		z80->writefuncs[num](addr, data);
	}
	else {
		printf("unhandled write $%04X = $%02X\n", addr, data);
	}
}

__inline u8 deadz80_ioread(u32 addr)
{
	return(z80->ioreadfunc(addr));
}

__inline void deadz80_iowrite(u32 addr, u8 data)
{
	z80->iowritefunc(addr,data);
}

void deadz80_reset()
{
	CYCLES = 0;			//reset cycle counter
	HALT = 0;			//clear halt flag
//	intmode = 0;
//	inside_irq = 0;

	z80->regs = &z80->alt;
	AF = 0xFFFF;
	BC = 0xFFFF;
	DE = 0xFFFF;
	HL = 0xFFFF;

	z80->regs = &z80->main;
	AF = 0xFFFF;
	BC = 0xFFFF;
	DE = 0xFFFF;
	HL = 0xFFFF;

	SP = 0xFFFF;		//reset sp
	PC = 0;				//reset pc
}

void deadz80_nmi()
 {
	IFF2 = IFF1;
	IFF1 = 0;
	write8(--SP, (PC >> 8) & 0xFF);
	write8(--SP, (PC >> 0) & 0xFF);
	PC = 0x66;
	CYCLES += 11;
}

void deadz80_irq()
{
	if (IFF1 == 0)
		return;

	if (HALT) {
		HALT = 0;
		PC++;
	}

	switch (INTMODE) {
	case 0:
		INSIDEIRQ = 1;
		deadz80_step();
		break;
	case 1:
		write8(--SP, (PC >> 8) & 0xFF);
		write8(--SP, (PC >> 0) & 0xFF);
		PC = 0x0038;
		CYCLES += 13;
		break;
	case 2:
		printf("im 2 not done\n");
		break;
	default:
		printf("bad im\n");
		break;
	}
}

//include all opcode macros and opcode execution functions
#include "opcodes.h"


__inline u16 read16(u32 addr)
{
	return((u16)((read8(addr + 1) << 8) | read8(addr)));
}

__inline void write16(u32 addr,u16 data)
{
	write8(addr, data & 0xFF);
	write8(addr + 1, (data >> 8) & 0xFF);
}

__inline void step_cb()
{
	unsigned char opcode = read8(PC++);
	unsigned char tmp, tmp2;

	switch (opcode) {
	case 0x00:	RLC(B);		CYCLES += 8;	break;
	case 0x01:	RLC(C);		CYCLES += 8;	break;
	case 0x02:	RLC(D);		CYCLES += 8;	break;
	case 0x03:	RLC(E);		CYCLES += 8;	break;
	case 0x04:	RLC(H);		CYCLES += 8;	break;
	case 0x05:	RLC(L);		CYCLES += 8;	break;
	case 0x06:	tmp2 = read8(HL);	RLC(tmp2);	write8(HL, tmp2);	CYCLES += 15;	break;
	case 0x07:	RLC(A);		CYCLES += 8;	break;
	case 0x08:	RRC(B);		CYCLES += 8;	break;
	case 0x09:	RRC(C);		CYCLES += 8;	break;
	case 0x0A:	RRC(D);		CYCLES += 8;	break;
	case 0x0B:	RRC(E);		CYCLES += 8;	break;
	case 0x0C:	RRC(H);		CYCLES += 8;	break;
	case 0x0D:	RRC(L);		CYCLES += 8;	break;
	case 0x0E:	tmp2 = read8(HL);	RRC(tmp2);	write8(HL, tmp2);	CYCLES += 15;	break;
	case 0x0F:	RRC(A);		CYCLES += 8;	break;
	case 0x10:	RL(B);		CYCLES += 8;	break;
	case 0x11:	RL(C);		CYCLES += 8;	break;
	case 0x12:	RL(D);		CYCLES += 8;	break;
	case 0x13:	RL(E);		CYCLES += 8;	break;
	case 0x14:	RL(H);		CYCLES += 8;	break;
	case 0x15:	RL(L);		CYCLES += 8;	break;
	case 0x16:	tmp2 = read8(HL);	RL(tmp2);	write8(HL, tmp2);	CYCLES += 15;	break;
	case 0x17:	RL(A);		CYCLES += 8;	break;
	case 0x18:	RR(B);		CYCLES += 8;	break;
	case 0x19:	RR(C);		CYCLES += 8;	break;
	case 0x1A:	RR(D);		CYCLES += 8;	break;
	case 0x1B:	RR(E);		CYCLES += 8;	break;
	case 0x1C:	RR(H);		CYCLES += 8;	break;
	case 0x1D:	RR(L);		CYCLES += 8;	break;
	case 0x1E:	tmp2 = read8(HL);	RR(tmp2);	write8(HL, tmp2);	CYCLES += 15;	break;
	case 0x1F:	RR(A);		CYCLES += 8;	break;
	case 0x20:	SLA(B);		CYCLES += 8;	break;
	case 0x21:	SLA(C);		CYCLES += 8;	break;
	case 0x22:	SLA(D);		CYCLES += 8;	break;
	case 0x23:	SLA(E);		CYCLES += 8;	break;
	case 0x24:	SLA(H);		CYCLES += 8;	break;
	case 0x25:	SLA(L);		CYCLES += 8;	break;
	case 0x26:	tmp = read8(HL);	SLA(tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x27:	SLA(A);		CYCLES += 8;	break;
	case 0x28:	SRA(B);		CYCLES += 8;	break;
	case 0x29:	SRA(C);		CYCLES += 8;	break;
	case 0x2A:	SRA(D);		CYCLES += 8;	break;
	case 0x2B:	SRA(E);		CYCLES += 8;	break;
	case 0x2C:	SRA(H);		CYCLES += 8;	break;
	case 0x2D:	SRA(L);		CYCLES += 8;	break;
	case 0x2E:	tmp = read8(HL);	SRA(tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x2F:	SRA(A);		CYCLES += 8;	break;
	case 0x30:	SLL(B);		CYCLES += 8;	break;
	case 0x31:	SLL(C);		CYCLES += 8;	break;
	case 0x32:	SLL(D);		CYCLES += 8;	break;
	case 0x33:	SLL(E);		CYCLES += 8;	break;
	case 0x34:	SLL(H);		CYCLES += 8;	break;
	case 0x35:	SLL(L);		CYCLES += 8;	break;
	case 0x36:	tmp = read8(HL);	SLL(tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x37:	SLL(A);		CYCLES += 8;	break;
	case 0x38:	SRL(B);		CYCLES += 8;	break;
	case 0x39:	SRL(C);		CYCLES += 8;	break;
	case 0x3A:	SRL(D);		CYCLES += 8;	break;
	case 0x3B:	SRL(E);		CYCLES += 8;	break;
	case 0x3C:	SRL(H);		CYCLES += 8;	break;
	case 0x3D:	SRL(L);		CYCLES += 8;	break;
	case 0x3E:	tmp = read8(HL);	SRL(tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x3F:	SRL(A);		CYCLES += 8;	break;
	case 0x40:	BIT(0, B);	CYCLES += 8;	break;
	case 0x41:	BIT(0, C);	CYCLES += 8;	break;
	case 0x42:	BIT(0, D);	CYCLES += 8;	break;
	case 0x43:	BIT(0, E);	CYCLES += 8;	break;
	case 0x44:	BIT(0, H);	CYCLES += 8;	break;
	case 0x45:	BIT(0, L);	CYCLES += 8;	break;
	case 0x46:	tmp = read8(HL);	BIT_HL(0, tmp);	CYCLES += 12;	break;
	case 0x47:	BIT(0, A);	CYCLES += 8;	break;
	case 0x48:	BIT(1, B);	CYCLES += 8;	break;
	case 0x49:	BIT(1, C);	CYCLES += 8;	break;
	case 0x4A:	BIT(1, D);	CYCLES += 8;	break;
	case 0x4B:	BIT(1, E);	CYCLES += 8;	break;
	case 0x4C:	BIT(1, H);	CYCLES += 8;	break;
	case 0x4D:	BIT(1, L);	CYCLES += 8;	break;
	case 0x4E:	tmp = read8(HL);	BIT_HL(1, tmp);	CYCLES += 12;	break;
	case 0x4F:	BIT(1, A);	CYCLES += 8;	break;
	case 0x50:	BIT(2, B);	CYCLES += 8;	break;
	case 0x51:	BIT(2, C);	CYCLES += 8;	break;
	case 0x52:	BIT(2, D);	CYCLES += 8;	break;
	case 0x53:	BIT(2, E);	CYCLES += 8;	break;
	case 0x54:	BIT(2, H);	CYCLES += 8;	break;
	case 0x55:	BIT(2, L);	CYCLES += 8;	break;
	case 0x56:	tmp = read8(HL);	BIT_HL(2, tmp);	CYCLES += 12;	break;
	case 0x57:	BIT(2, A);	CYCLES += 8;	break;
	case 0x58:	BIT(3, B);	CYCLES += 8;	break;
	case 0x59:	BIT(3, C);	CYCLES += 8;	break;
	case 0x5A:	BIT(3, D);	CYCLES += 8;	break;
	case 0x5B:	BIT(3, E);	CYCLES += 8;	break;
	case 0x5C:	BIT(3, H);	CYCLES += 8;	break;
	case 0x5D:	BIT(3, L);	CYCLES += 8;	break;
	case 0x5E:	tmp = read8(HL);	BIT_HL(3, tmp);	CYCLES += 12;	break;
	case 0x5F:	BIT(3, A);	CYCLES += 8;	break;
	case 0x60:	BIT(4, B);	CYCLES += 8;	break;
	case 0x61:	BIT(4, C);	CYCLES += 8;	break;
	case 0x62:	BIT(4, D);	CYCLES += 8;	break;
	case 0x63:	BIT(4, E);	CYCLES += 8;	break;
	case 0x64:	BIT(4, H);	CYCLES += 8;	break;
	case 0x65:	BIT(4, L);	CYCLES += 8;	break;
	case 0x66:	tmp = read8(HL);	BIT_HL(4, tmp);	CYCLES += 12;	break;
	case 0x67:	BIT(4, A);	CYCLES += 8;	break;
	case 0x68:	BIT(5, B);	CYCLES += 8;	break;
	case 0x69:	BIT(5, C);	CYCLES += 8;	break;
	case 0x6A:	BIT(5, D);	CYCLES += 8;	break;
	case 0x6B:	BIT(5, E);	CYCLES += 8;	break;
	case 0x6C:	BIT(5, H);	CYCLES += 8;	break;
	case 0x6D:	BIT(5, L);	CYCLES += 8;	break;
	case 0x6E:	tmp = read8(HL);	BIT_HL(5, tmp);	CYCLES += 12;	break;
	case 0x6F:	BIT(5, A);	CYCLES += 8;	break;
	case 0x70:	BIT(6, B);	CYCLES += 8;	break;
	case 0x71:	BIT(6, C);	CYCLES += 8;	break;
	case 0x72:	BIT(6, D);	CYCLES += 8;	break;
	case 0x73:	BIT(6, E);	CYCLES += 8;	break;
	case 0x74:	BIT(6, H);	CYCLES += 8;	break;
	case 0x75:	BIT(6, L);	CYCLES += 8;	break;
	case 0x76:	tmp = read8(HL);	BIT_HL(6, tmp);	CYCLES += 12;	break;
	case 0x77:	BIT(6, A);	CYCLES += 8;	break;
	case 0x78:	BIT(7, B);	CYCLES += 8;	break;
	case 0x79:	BIT(7, C);	CYCLES += 8;	break;
	case 0x7A:	BIT(7, D);	CYCLES += 8;	break;
	case 0x7B:	BIT(7, E);	CYCLES += 8;	break;
	case 0x7C:	BIT(7, H);	CYCLES += 8;	break;
	case 0x7D:	BIT(7, L);	CYCLES += 8;	break;
	case 0x7E:	tmp = read8(HL);	BIT_HL(7, tmp);	CYCLES += 12;	break;
	case 0x7F:	BIT(7, A);	CYCLES += 8;	break;
	case 0x80:	RES(0, B);	CYCLES += 8;	break;
	case 0x81:	RES(0, C);	CYCLES += 8;	break;
	case 0x82:	RES(0, D);	CYCLES += 8;	break;
	case 0x83:	RES(0, E);	CYCLES += 8;	break;
	case 0x84:	RES(0, H);	CYCLES += 8;	break;
	case 0x85:	RES(0, L);	CYCLES += 8;	break;
	case 0x86:	tmp = read8(HL);	RES(0, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x87:	RES(0, A);	CYCLES += 8;	break;
	case 0x88:	RES(1, B);	CYCLES += 8;	break;
	case 0x89:	RES(1, C);	CYCLES += 8;	break;
	case 0x8A:	RES(1, D);	CYCLES += 8;	break;
	case 0x8B:	RES(1, E);	CYCLES += 8;	break;
	case 0x8C:	RES(1, H);	CYCLES += 8;	break;
	case 0x8D:	RES(1, L);	CYCLES += 8;	break;
	case 0x8E:	tmp = read8(HL);	RES(1, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x8F:	RES(1, A);	CYCLES += 8;	break;
	case 0x90:	RES(2, B);	CYCLES += 8;	break;
	case 0x91:	RES(2, C);	CYCLES += 8;	break;
	case 0x92:	RES(2, D);	CYCLES += 8;	break;
	case 0x93:	RES(2, E);	CYCLES += 8;	break;
	case 0x94:	RES(2, H);	CYCLES += 8;	break;
	case 0x95:	RES(2, L);	CYCLES += 8;	break;
	case 0x96:	tmp = read8(HL);	RES(2, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x97:	RES(2, A);	CYCLES += 8;	break;
	case 0x98:	RES(3, B);	CYCLES += 8;	break;
	case 0x99:	RES(3, C);	CYCLES += 8;	break;
	case 0x9A:	RES(3, D);	CYCLES += 8;	break;
	case 0x9B:	RES(3, E);	CYCLES += 8;	break;
	case 0x9C:	RES(3, H);	CYCLES += 8;	break;
	case 0x9D:	RES(3, L);	CYCLES += 8;	break;
	case 0x9E:	tmp = read8(HL);	RES(3, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0x9F:	RES(3, A);	CYCLES += 8;	break;
	case 0xA0:	RES(4, B);	CYCLES += 8;	break;
	case 0xA1:	RES(4, C);	CYCLES += 8;	break;
	case 0xA2:	RES(4, D);	CYCLES += 8;	break;
	case 0xA3:	RES(4, E);	CYCLES += 8;	break;
	case 0xA4:	RES(4, H);	CYCLES += 8;	break;
	case 0xA5:	RES(4, L);	CYCLES += 8;	break;
	case 0xA6:	tmp = read8(HL);	RES(4, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xA7:	RES(4, A);	CYCLES += 8;	break;
	case 0xA8:	RES(5, B);	CYCLES += 8;	break;
	case 0xA9:	RES(5, C);	CYCLES += 8;	break;
	case 0xAA:	RES(5, D);	CYCLES += 8;	break;
	case 0xAB:	RES(5, E);	CYCLES += 8;	break;
	case 0xAC:	RES(5, H);	CYCLES += 8;	break;
	case 0xAD:	RES(5, L);	CYCLES += 8;	break;
	case 0xAE:	tmp = read8(HL);	RES(5, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xAF:	RES(5, A);	CYCLES += 8;	break;
	case 0xB0:	RES(6, B);	CYCLES += 8;	break;
	case 0xB1:	RES(6, C);	CYCLES += 8;	break;
	case 0xB2:	RES(6, D);	CYCLES += 8;	break;
	case 0xB3:	RES(6, E);	CYCLES += 8;	break;
	case 0xB4:	RES(6, H);	CYCLES += 8;	break;
	case 0xB5:	RES(6, L);	CYCLES += 8;	break;
	case 0xB6:	tmp = read8(HL);	RES(6, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xB7:	RES(6, A);	CYCLES += 8;	break;
	case 0xB8:	RES(7, B);	CYCLES += 8;	break;
	case 0xB9:	RES(7, C);	CYCLES += 8;	break;
	case 0xBA:	RES(7, D);	CYCLES += 8;	break;
	case 0xBB:	RES(7, E);	CYCLES += 8;	break;
	case 0xBC:	RES(7, H);	CYCLES += 8;	break;
	case 0xBD:	RES(7, L);	CYCLES += 8;	break;
	case 0xBE:	tmp = read8(HL);	RES(7, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xBF:	RES(7, A);	CYCLES += 8;	break;
	case 0xC0:	SET(0, B);	CYCLES += 8;	break;
	case 0xC1:	SET(0, C);	CYCLES += 8;	break;
	case 0xC2:	SET(0, D);	CYCLES += 8;	break;
	case 0xC3:	SET(0, E);	CYCLES += 8;	break;
	case 0xC4:	SET(0, H);	CYCLES += 8;	break;
	case 0xC5:	SET(0, L);	CYCLES += 8;	break;
	case 0xC6:	tmp = read8(HL);	SET(0, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xC7:	SET(0, A);	CYCLES += 8;	break;
	case 0xC8:	SET(1, B);	CYCLES += 8;	break;
	case 0xC9:	SET(1, C);	CYCLES += 8;	break;
	case 0xCA:	SET(1, D);	CYCLES += 8;	break;
	case 0xCB:	SET(1, E);	CYCLES += 8;	break;
	case 0xCC:	SET(1, H);	CYCLES += 8;	break;
	case 0xCD:	SET(1, L);	CYCLES += 8;	break;
	case 0xCE:	tmp = read8(HL);	SET(1, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xCF:	SET(1, A);	CYCLES += 8;	break;
	case 0xD0:	SET(2, B);	CYCLES += 8;	break;
	case 0xD1:	SET(2, C);	CYCLES += 8;	break;
	case 0xD2:	SET(2, D);	CYCLES += 8;	break;
	case 0xD3:	SET(2, E);	CYCLES += 8;	break;
	case 0xD4:	SET(2, H);	CYCLES += 8;	break;
	case 0xD5:	SET(2, L);	CYCLES += 8;	break;
	case 0xD6:	tmp = read8(HL);	SET(2, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xD7:	SET(2, A);	CYCLES += 8;	break;
	case 0xD8:	SET(3, B);	CYCLES += 8;	break;
	case 0xD9:	SET(3, C);	CYCLES += 8;	break;
	case 0xDA:	SET(3, D);	CYCLES += 8;	break;
	case 0xDB:	SET(3, E);	CYCLES += 8;	break;
	case 0xDC:	SET(3, H);	CYCLES += 8;	break;
	case 0xDD:	SET(3, L);	CYCLES += 8;	break;
	case 0xDE:	tmp = read8(HL);	SET(3, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xDF:	SET(3, A);	CYCLES += 8;	break;
	case 0xE0:	SET(4, B);	CYCLES += 8;	break;
	case 0xE1:	SET(4, C);	CYCLES += 8;	break;
	case 0xE2:	SET(4, D);	CYCLES += 8;	break;
	case 0xE3:	SET(4, E);	CYCLES += 8;	break;
	case 0xE4:	SET(4, H);	CYCLES += 8;	break;
	case 0xE5:	SET(4, L);	CYCLES += 8;	break;
	case 0xE6:	tmp = read8(HL);	SET(4, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xE7:	SET(4, A);	CYCLES += 8;	break;
	case 0xE8:	SET(5, B);	CYCLES += 8;	break;
	case 0xE9:	SET(5, C);	CYCLES += 8;	break;
	case 0xEA:	SET(5, D);	CYCLES += 8;	break;
	case 0xEB:	SET(5, E);	CYCLES += 8;	break;
	case 0xEC:	SET(5, H);	CYCLES += 8;	break;
	case 0xED:	SET(5, L);	CYCLES += 8;	break;
	case 0xEE:	tmp = read8(HL);	SET(5, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xEF:	SET(5, A);	CYCLES += 8;	break;
	case 0xF0:	SET(6, B);	CYCLES += 8;	break;
	case 0xF1:	SET(6, C);	CYCLES += 8;	break;
	case 0xF2:	SET(6, D);	CYCLES += 8;	break;
	case 0xF3:	SET(6, E);	CYCLES += 8;	break;
	case 0xF4:	SET(6, H);	CYCLES += 8;	break;
	case 0xF5:	SET(6, L);	CYCLES += 8;	break;
	case 0xF6:	tmp = read8(HL);	SET(6, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xF7:	SET(6, A);	CYCLES += 8;	break;
	case 0xF8:	SET(7, B);	CYCLES += 8;	break;
	case 0xF9:	SET(7, C);	CYCLES += 8;	break;
	case 0xFA:	SET(7, D);	CYCLES += 8;	break;
	case 0xFB:	SET(7, E);	CYCLES += 8;	break;
	case 0xFC:	SET(7, H);	CYCLES += 8;	break;
	case 0xFD:	SET(7, L);	CYCLES += 8;	break;
	case 0xFE:	tmp = read8(HL);	SET(7, tmp);	write8(HL, tmp);	CYCLES += 15;	break;
	case 0xFF:	SET(7, A);	CYCLES += 8;	break;
	default:
		printf("bad CB opcode $%02X\n", opcode);
		break;
	}
}

__inline void step_ddcb()
{
	unsigned char data = read8(PC++);
	unsigned char opcode = read8(PC++);
	unsigned char tmp, tmp2;
	unsigned long ltmp;

	ltmp = (unsigned long)(IX + (signed char)data);
		switch (opcode) {
		case 0x01:
			tmp2 = read8(ltmp);
			RLC(tmp2);
			write8(ltmp, tmp2);
			C = tmp2;
			CYCLES += 23;
			break; //rlc (ix+n),c
		case 0x06: tmp2 = read8(ltmp); RLC(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //rlc (ix+n)
		case 0x0E: tmp2 = read8(ltmp); RRC(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //rrc (ix+n)
		case 0x16: tmp2 = read8(ltmp); RL(tmp2);  write8(ltmp, tmp2); CYCLES += 23; break; //rl (ix+n)
		case 0x1E: tmp2 = read8(ltmp); RR(tmp2);  write8(ltmp, tmp2); CYCLES += 23; break; //rr (ix+n)
		case 0x26: tmp2 = read8(ltmp); SLA(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sla (ix+n)
		case 0x2E: tmp2 = read8(ltmp); SRA(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sra (ix+n)
		case 0x36: tmp2 = read8(ltmp); SLL(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sla (ix+n)
		case 0x3E: tmp2 = read8(ltmp); SRL(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sra (ix+n)
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:	//bit 0,(ix+n)
		case 0x47:	BIT_IDX(0);							break;
		case 0x48:
		case 0x49:
		case 0x4A:
		case 0x4B:
		case 0x4C:
		case 0x4D:
		case 0x4E:	//bit 1,(ix+n)
		case 0x4F:	BIT_IDX(1);							break;
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56:	//bit 2,(ix+n)
		case 0x57:	BIT_IDX(2);							break;
		case 0x58:
		case 0x59:
		case 0x5A:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x5E:	//bit 3,(ix+n)
		case 0x5F:	BIT_IDX(3);							break;
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case 0x65:
		case 0x66:	//bit 4,(ix+n)
		case 0x67:	BIT_IDX(4);							break;
		case 0x68:
		case 0x69:
		case 0x6A:
		case 0x6B:
		case 0x6C:
		case 0x6D:
		case 0x6E:	//bit 5,(ix+n)
		case 0x6F:	BIT_IDX(5);							break;
		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:
		case 0x74:
		case 0x75:
		case 0x76:	//bit 6,(ix+n)
		case 0x77:	BIT_IDX(6);							break;
		case 0x78:
		case 0x79:
		case 0x7A:
		case 0x7B:
		case 0x7C:
		case 0x7D:
		case 0x7E:	//bit 7,(ix+n)
		case 0x7F:	BIT_IDX(7);							break;
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:	//res 0,(ix+n)
		case 0x87:	RES_IDX(0);							break;
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
		case 0x8D:
		case 0x8E:	//res 1,(ix+n)
		case 0x8F:	RES_IDX(1);							break;
		case 0x90:
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:	//res 2,(ix+n)
		case 0x97:	RES_IDX(2);							break;
		case 0x98:
		case 0x99:
		case 0x9A:
		case 0x9B:
		case 0x9C:
		case 0x9D:
		case 0x9E:	//res 3,(ix+n)
		case 0x9F:	RES_IDX(3);							break;
		case 0xA0:
		case 0xA1:
		case 0xA2:
		case 0xA3:
		case 0xA4:
		case 0xA5:
		case 0xA6:	//res 4,(ix+n)
		case 0xA7:	RES_IDX(4);							break;
		case 0xA8:
		case 0xA9:
		case 0xAA:
		case 0xAB:
		case 0xAC:
		case 0xAD:
		case 0xAE:	//res 5,(ix+n)
		case 0xAF:	RES_IDX(5);							break;
		case 0xB0:
		case 0xB1:
		case 0xB2:
		case 0xB3:
		case 0xB4:
		case 0xB5:
		case 0xB6:	//res 6,(ix+n)
		case 0xB7:	RES_IDX(6);							break;
		case 0xB8:
		case 0xB9:
		case 0xBA:
		case 0xBB:
		case 0xBC:
		case 0xBD:
		case 0xBE:	//res 7,(ix+n)
		case 0xBF:	RES_IDX(7);							break;
		case 0xC0:
		case 0xC1:
		case 0xC2:
		case 0xC3:
		case 0xC4:
		case 0xC5:
		case 0xC6:	//set 0,(ix+n)
		case 0xC7:	SET_IDX(0);							break;
		case 0xC8:
		case 0xC9:
		case 0xCA:
		case 0xCB:
		case 0xCC:
		case 0xCD:
		case 0xCE:	//set 1,(ix+n)
		case 0xCF:	SET_IDX(1);							break;
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
		case 0xD4:
		case 0xD5:
		case 0xD6:	//set 2,(ix+n)
		case 0xD7:	SET_IDX(2);							break;
		case 0xD8:
		case 0xD9:
		case 0xDA:
		case 0xDB:
		case 0xDC:
		case 0xDD:
		case 0xDE:	//set 3,(ix+n)
		case 0xDF:	SET_IDX(3);							break;
		case 0xE0:
		case 0xE1:
		case 0xE2:
		case 0xE3:
		case 0xE4:
		case 0xE5:
		case 0xE6:	//set 4,(ix+n)
		case 0xE7:	SET_IDX(4);							break;
		case 0xE8:
		case 0xE9:
		case 0xEA:
		case 0xEB:
		case 0xEC:
		case 0xED:
		case 0xEE:	//set 5,(ix+n)
		case 0xEF:	SET_IDX(5);							break;
		case 0xF0:
		case 0xF1:
		case 0xF2:
		case 0xF3:
		case 0xF4:
		case 0xF5:
		case 0xF6:	//set 6,(ix+n)
		case 0xF7:	SET_IDX(6);							break;
		case 0xF8:
		case 0xF9:
		case 0xFA:
		case 0xFB:
		case 0xFC:
		case 0xFD:
		case 0xFE:	//set 7,(ix+n)
		case 0xFF:	SET_IDX(7);							break;

		default:
			printf("bad DDCB opcode = $%02X\n", opcode);
			break;
	}
}

__inline void step_dd()
{
	unsigned char opcode = read8(PC++);
	unsigned short stmp;
	unsigned char tmp;
	unsigned long ltmp, otmp;

	switch (opcode) {
	case 0x09:	ADD16(IX, BC);				CYCLES += 8;	break;
	case 0x19:	ADD16(IX, DE);				CYCLES += 8;	break;

	case 0x21:	//ld IX,nn
		IX = read16(PC);
		PC += 2;
		CYCLES += 14;
		break;
	case 0x22:	//ld (nn),IX
		write16(read16(PC), IX);
		PC += 2;
		CYCLES += 20;
		break;
	case 0x23:	//inc IX
		IX++;
		CYCLES += 10;
		break;
	case 0x24:	//inc ixh
		tmp = (u8)(IXH);
		INC(tmp);
		IXH = tmp;
		CYCLES += 8;
		break;
	case 0x25:	//dec ixh
		tmp = (u8)(IXH);
		DEC(tmp);
		IXH = tmp;
		CYCLES += 8;
		break;

	case 0x26:	//ld ixh,n
		IX = (read8(PC++) << 8) | (IX & 0xFF);
		CYCLES += 11;
		break;
	case 0x29:	ADD16(IX, IX);	CYCLES += 8;	break;
	case 0x39:	ADD16(IX, SP);	CYCLES += 8;	break;

	case 0x2A:	//ld IX,(nn)
		IX = read16(read16(PC));
		PC += 2;
		CYCLES += 20;
		break;
	case 0x2B:	//dec IX
		IX--;
		CYCLES += 10;
		break;
	case 0x2C:	//inc ixl
		tmp = (u8)(IXL);
		INC(tmp);
		IXL = tmp;
		CYCLES += 8;
		break;
	case 0x2D:	//dec ixl
		tmp = (u8)(IXL);
		DEC(tmp);
		IXL = tmp;
		CYCLES += 8;
		break;
	case 0x2E:	//ld ixl,n
		IX = read8(PC++) | (IX & 0xFF00);
		CYCLES += 11;
		break;

	case 0x34:	//inc (IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		tmp = read8(ltmp);
		INC(tmp);
		write8(ltmp, tmp);
		CYCLES += 23;
		break;

	case 0x35:	//dec (IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		tmp = read8(ltmp);
		DEC(tmp);
		write8(ltmp, tmp);
		CYCLES += 23;
		break;

	case 0x36:	//ld (IX+d),n
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, read8(PC++));
		CYCLES += 19;
		break;

	case 0x40:	B = B;								CYCLES += 8;	break;
	case 0x41:	B = C;								CYCLES += 8;	break;
	case 0x42:	B = D;								CYCLES += 8;	break;
	case 0x43:	B = E;								CYCLES += 8;	break;
	case 0x44:	B = IXH;							CYCLES += 8;	break;
	case 0x45:	B = IXL;							CYCLES += 8;	break;
	case 0x46:	//ld b,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		B = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x47:	B = A;								CYCLES += 8;	break;
	case 0x48:	C = B;								CYCLES += 8;	break;
	case 0x49:	C = C;								CYCLES += 8;	break;
	case 0x4A:	C = D;								CYCLES += 8;	break;
	case 0x4B:	C = E;								CYCLES += 8;	break;
	case 0x4C:	C = IXH;							CYCLES += 8;	break;
	case 0x4D:	C = IXL;							CYCLES += 8;	break;
	case 0x4E:	//ld c,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		C = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x4F:	C = A;								CYCLES += 8;	break;
	case 0x50:	D = B;								CYCLES += 8;	break;
	case 0x51:	D = C;								CYCLES += 8;	break;
	case 0x52:	D = D;								CYCLES += 8;	break;
	case 0x53:	D = E;								CYCLES += 8;	break;
	case 0x54:	D = IXH;							CYCLES += 8;	break;
	case 0x55:	D = IXL;							CYCLES += 8;	break;
	case 0x56:	//ld d,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		D = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x57:	D = A;								CYCLES += 8;	break;
	case 0x58:	E = B;								CYCLES += 8;	break;
	case 0x59:	E = C;								CYCLES += 8;	break;
	case 0x5A:	E = D;								CYCLES += 8;	break;
	case 0x5B:	E = E;								CYCLES += 8;	break;
	case 0x5C:	E = IXH;							CYCLES += 8;	break;
	case 0x5D:	E = IXL;							CYCLES += 8;	break;
	case 0x5E:	//ld e,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		E = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x5F:	E = A;								CYCLES += 8;	break;
	case 0x60:	IXH = B;							CYCLES += 8;	break;
	case 0x61:	IXH = C;							CYCLES += 8;	break;
	case 0x62:	IXH = D;							CYCLES += 8;	break;
	case 0x63:	IXH = E;							CYCLES += 8;	break;
	case 0x64:	IXH = IXH;							CYCLES += 8;	break;
	case 0x65:	IXH = IXL;							CYCLES += 8;	break;
	case 0x66:	//ld h,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		H = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x67:	IXH = A;							CYCLES += 8;	break;
	case 0x68:	IXL = B;							CYCLES += 8;	break;
	case 0x69:	IXL = C;							CYCLES += 8;	break;
	case 0x6A:	IXL = D;							CYCLES += 8;	break;
	case 0x6B:	IXL = E;							CYCLES += 8;	break;
	case 0x6C:	IXL = IXH;							CYCLES += 8;	break;
	case 0x6D:	IXL = IXL;							CYCLES += 8;	break;
	case 0x6E:	//ld l,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		L = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x6F:	IXL = A;								CYCLES += 8;	break;

	case 0x70:	//ld (IX+d),b
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, B);
		CYCLES += 19;
		break;
	case 0x71:	//ld (IX+d),c
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, C);
		CYCLES += 19;
		break;
	case 0x72:	//ld (IX+d),d
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, D);
		CYCLES += 19;
		break;
	case 0x73:	//ld (IX+d),e
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, E);
		CYCLES += 19;
		break;
	case 0x74:	//ld (IX+d),h
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, H);
		CYCLES += 19;
		break;
	case 0x75:	//ld (IX+d),l
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, L);
		CYCLES += 19;
		break;

	case 0x77:	//ld (IX+d),a
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		write8(ltmp, A);
		CYCLES += 19;
		break;
	case 0x78:	A = B;								CYCLES += 8;	break;
	case 0x79:	A = C;								CYCLES += 8;	break;
	case 0x7A:	A = D;								CYCLES += 8;	break;
	case 0x7B:	A = E;								CYCLES += 8;	break;
	case 0x7C:	A = IXH;							CYCLES += 8;	break;
	case 0x7D:	A = IXL;							CYCLES += 8;	break;
	case 0x7E:	//ld a,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		A = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x7F:	A = A;								CYCLES += 8;	break;
	case 0x84:	ADD(IXH);							CYCLES += 8;	break;
	case 0x85:	ADD(IXL);							CYCLES += 8;	break;
	case 0x86:	//add a,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		ADD(read8(ltmp));
		CYCLES += 19;
		break;
	case 0x8C:	ADC(IXH);							CYCLES += 8;	break;
	case 0x8D:	ADC(IXL);							CYCLES += 8;	break;
	case 0x8E:	//adc a,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		ADC(read8(ltmp));
		CYCLES += 19;
		break;
	case 0x94:	SUB(IXH);							CYCLES += 8;	break;
	case 0x95:	SUB(IXL);							CYCLES += 8;	break;
	case 0x96:	//sub a,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		SUB(read8(ltmp));
		CYCLES += 19;
		break;
	case 0x9C:	SBC(IXH);							CYCLES += 8;	break;
	case 0x9D:	SBC(IXL);							CYCLES += 8;	break;
	case 0x9E:	//sbc a,(IX+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		SBC(read8(ltmp));
		CYCLES += 19;
		break;
	case 0xA4:	AND(IXH);							CYCLES += 8;	break;
	case 0xA5:	AND(IXL);							CYCLES += 8;	break;
	case 0xA6:
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		tmp = read8(ltmp);
		AND(tmp);
		CYCLES += 19;
		break;
	case 0xAC:	XOR(IXH);							CYCLES += 8;	break;
	case 0xAD:	XOR(IXL);							CYCLES += 8;	break;
	case 0xAE:
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		tmp = read8(ltmp);
		XOR(tmp);
		CYCLES += 19;
		break;
	case 0xB4:	OR(IXH);							CYCLES += 8;	break;
	case 0xB5:	OR(IXL);							CYCLES += 8;	break;
	case 0xB6:
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		tmp = read8(ltmp);
		OR(tmp);
		CYCLES += 19;
		break;
	case 0xBC:	CP(IXH);							CYCLES += 8;	break;
	case 0xBD:	CP(IXL);							CYCLES += 8;	break;
	case 0xBE:
		ltmp = (unsigned long)(unsigned short)((signed short)IX + (signed char)read8(PC++));
		tmp = read8(ltmp);
		CP(tmp);
		CYCLES += 19;
		break;
	case 0xCB:	step_ddcb(); break;
	case 0xE1:	//pop IX
		POP16(IX);
		CYCLES += 14;
		break;
	case 0xE5:	//push IX
		PUSH16(IX);
		CYCLES += 15;
		break;
	default:
		printf("bad DD opcode $%02X\n", opcode);
		break;
	}
}

__inline void step_ed()
{
	unsigned char opcode = read8(PC++);
	unsigned short stmp, tmp16;
	unsigned char tmp;
	unsigned long ltmp;
	int itmp;

	switch (opcode) {
	case 0x42:	SBC16(HL, BC);	CYCLES += 15;	break;	//sbc hl,bc
	case 0x52:	SBC16(HL, DE);	CYCLES += 15;	break;	//sbc hl,de
	case 0x62:	SBC16(HL, HL);	CYCLES += 15;	break;	//sbc hl,hl
	case 0x72:	SBC16(HL, SP);	CYCLES += 15;	break;	//sbc hl,hl
	case 0x7A:	ADC16(HL, SP);	CYCLES += 15;	break;	//sbc hl,hl
	case 0x43:	//ld (nn),bc
		write16(read16(PC), BC);
		PC += 2;
		CYCLES += 20;
		break;
	case 0x44:	//neg
		itmp = -A;
		stmp = A ^ itmp;
		F = FLAG_N | (stmp & FLAG_H);
		F |= (stmp >= 0x100) ? FLAG_C : 0;
		F |= (itmp == 0) ? FLAG_Z : 0;
		F |= (itmp & 0x80) ? FLAG_S : 0;
		F |= (A == 0x80) ? FLAG_V : 0;
		F |= (itmp & 0x28);
		A = itmp;
		CYCLES += 8;
		break;
	case 0x4A:	ADC16(HL, BC);	CYCLES += 15;	break;	//sbc hl,bc
	case 0x4B:	//ld bc,(nn)
		BC = read16(read16(PC));
		PC += 2;
		CYCLES += 20;
		break;
	case 0x53:	//ld (nn),de
		write16(read16(PC), DE);
		PC += 2;
		CYCLES += 20;
		break;
	case 0x56:	//im 1
		z80->intmode = 1;
		CYCLES += 8;
		break;
	case 0x5A:	ADC16(HL, DE);		CYCLES += 15;	break;

	case 0x5B:	//ld de,(nn)
		DE = read16(read16(PC));
		PC += 2;
		CYCLES += 20;
		break;
	case 0x67: //rrd
		tmp = read8(HL);
		write8(HL, (tmp >> 4) | (A << 4));
		A = (A & 0xF0) | (tmp & 0xF);
		F &= FLAG_C;
		F |= A & 0x28;
		if (A == 0)
			F |= FLAG_Z;
		if (A & 0x80)
			F |= FLAG_S;
		if (parity[A])
			F |= FLAG_P;
		CYCLES += 18;
		break;
	case 0x6A:	ADC16(HL, HL);		CYCLES += 15;	break;

	case 0x6B:	//ld hl,(nn)
		HL = read16(read16(PC));
		PC += 2;
		CYCLES += 20;
		break;
	case 0x6F:
		tmp = read8(HL);
		write8(HL, (tmp << 4) | (A & 0xF));
		A = (A & 0xF0) | (tmp >> 4);
		F &= FLAG_C;
		F |= A & 0x28;
		if (A == 0)
			F |= FLAG_Z;
		if (A & 0x80)
			F |= FLAG_S;
		if (parity[A])
			F |= FLAG_P;
		CYCLES += 18;
		break;
	case 0x7B:	//ld SP,(nn)
		SP = read16(read16(PC));
		PC += 2;
		CYCLES += 20;
		break;

	case 0x73:	//LD (nn),SP
		write16(read16(PC), SP);
		PC += 2;
		CYCLES += 20;
		break;

	case 0xA0:	//ldi
		stmp = read8(HL++);
		write8(DE++, stmp);
		F &= (FLAG_S | FLAG_Z | FLAG_C);
		F |= --BC ? FLAG_P : 0;
		stmp += A;
		F |= (stmp & 8) | ((stmp << 4) & 0x20);
		CYCLES += 16;
		break;

	case 0xA1:	//cpi
		tmp = read8(HL++);
		stmp = A - tmp;
		BC--;
		F = (F & FLAG_C) | FLAG_N;
		F |= (A ^ tmp ^ stmp) & FLAG_H;
		F |= (BC ? FLAG_P : 0);
		F |= stmp & FLAG_S;
		F |= (stmp == 0) ? FLAG_Z : 0;
		stmp -= (F >> 4) & 1;
		F |= (stmp << 4) & 0x20;
		F |= stmp & 0x08;
		CYCLES += 16;
		break;
	case 0xA3:	//outi
		B--;
		z80->iowritefunc(BC, read8(HL++));
		CYCLES += 16;
		break;
	case 0xA8:	//ldd
		stmp = read8(HL--);
		write8(DE--, stmp);
		F &= (FLAG_S | FLAG_Z | FLAG_C);
		F |= --BC ? FLAG_P : 0;
		stmp += A;
		F |= (stmp & 8) | ((stmp << 4) & 0x20);
		CYCLES += 16;
		break;

	case 0xA9:	//cpd
		tmp = read8(HL--);
		stmp = A - tmp;
		BC--;
		F = (F & FLAG_C) | FLAG_N;
		F |= (A ^ tmp ^ stmp) & FLAG_H;
		F |= (BC ? FLAG_P : 0);
		F |= stmp & FLAG_S;
		F |= (stmp == 0) ? FLAG_Z : 0;
		stmp -= (F >> 4) & 1;
		F |= (stmp << 4) & 0x20;
		F |= stmp & 0x08;
		CYCLES += 16;
		break;

	case 0xB0:	//ldir
		tmp = read8(HL);
		write8(DE, tmp);
		DE++;
		HL++;
		BC--;
		if (BC != 0) {
			PC -= 2;
			CYCLES += 21;
			F &= ~(FLAG_N | FLAG_H | 0x28);
			F |= FLAG_P;
		}
		else {
			F &= ~(FLAG_H | FLAG_P | FLAG_N | 0x28);
			CYCLES += 16;
		}
		//	printf("ldir done: PC = $%04X\n",PC);
		tmp += A;
		F |= tmp & 8;
		F |= (tmp & 2) << 4;
		break;
	case 0xB1:	//cpir
		tmp = read8(HL++);
		stmp = A - tmp;
		if (--BC && stmp) {
			CYCLES += 21;
			PC -= 2;
		}
		else {
			CYCLES += 16;
		}

		F = (F & FLAG_C) | FLAG_N;
		F |= (A ^ tmp ^ stmp) & FLAG_H;
		F |= (BC ? FLAG_P : 0);
		F |= stmp & FLAG_S;
		F |= (stmp == 0) ? FLAG_Z : 0;

		//calculate the x and y flags
		stmp -= (F >> 4) & 1;
		F |= (stmp << 4) & 0x20;
		F |= stmp & 0x08;
		break;

	case 0xB3:	//otir
		B--;
		z80->iowritefunc(BC, read8(HL++));
		PC -= 2;
		break;

	case 0xB8:	//lddr
		stmp = read8(HL--);
		write8(DE--, stmp);
		F &= (FLAG_S | FLAG_Z | FLAG_C);
		F |= --BC ? FLAG_P : 0;
		stmp += A;
		F |= (stmp & 8) | ((stmp << 4) & 0x20);
		if (BC > 0) {
			CYCLES += 21;
			PC -= 2;
		}
		else {
			CYCLES += 16;
		}
		break;

	case 0xB9:	//cpdr
		tmp = read8(HL--);
		stmp = A - tmp;
		if (--BC && stmp) {
			PC -= 2;
			CYCLES += 21;
		}
		else {
			CYCLES += 16;
		}

		F = (F & FLAG_C) | FLAG_N;
		F |= (A ^ tmp ^ stmp) & FLAG_H;
		F |= (BC ? FLAG_P : 0);
		F |= stmp & FLAG_S;
		F |= (stmp == 0) ? FLAG_Z : 0;

		//calculate the x and y flags
		stmp -= (F >> 4) & 1;
		F |= (stmp << 4) & 0x20;
		F |= stmp & 0x08;
		break;

	default:
		printf("bad ED opcode $%02X\n", opcode);
		break;
	}
}

__inline void step_fdcb()
{
	unsigned char data = read8(PC++);
	unsigned char opcode = read8(PC++);
	unsigned short stmp, tmp16;
	unsigned char tmp, tmp2;
	unsigned long ltmp;

	ltmp = (unsigned int)(unsigned short)((signed short)IY + (signed char)data);	\
		switch (opcode) {
		case 0x06: tmp2 = read8(ltmp); RLC(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //rlc (ix+n)
		case 0x0E: tmp2 = read8(ltmp); RRC(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //rrc (ix+n)
		case 0x16: tmp2 = read8(ltmp); RL(tmp2);  write8(ltmp, tmp2); CYCLES += 23; break; //rl (ix+n)
		case 0x1E: tmp2 = read8(ltmp); RR(tmp2);  write8(ltmp, tmp2); CYCLES += 23; break; //rr (ix+n)
		case 0x26: tmp2 = read8(ltmp); SLA(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sla (ix+n)
		case 0x2E: tmp2 = read8(ltmp); SRA(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sra (ix+n)
		case 0x36: tmp2 = read8(ltmp); SLL(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sla (ix+n)
		case 0x3E: tmp2 = read8(ltmp); SRL(tmp2); write8(ltmp, tmp2); CYCLES += 23; break; //sra (ix+n)
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:	//bit 0,(ix+n)
		case 0x47:	BIT_IDX(0);							break;
		case 0x48:
		case 0x49:
		case 0x4A:
		case 0x4B:
		case 0x4C:
		case 0x4D:
		case 0x4E:	//bit 1,(ix+n)
		case 0x4F:	BIT_IDX(1);							break;
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56:	//bit 2,(ix+n)
		case 0x57:	BIT_IDX(2);							break;
		case 0x58:
		case 0x59:
		case 0x5A:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x5E:	//bit 3,(ix+n)
		case 0x5F:	BIT_IDX(3);							break;
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case 0x65:
		case 0x66:	//bit 4,(ix+n)
		case 0x67:	BIT_IDX(4);							break;
		case 0x68:
		case 0x69:
		case 0x6A:
		case 0x6B:
		case 0x6C:
		case 0x6D:
		case 0x6E:	//bit 5,(ix+n)
		case 0x6F:	BIT_IDX(5);							break;
		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:
		case 0x74:
		case 0x75:
		case 0x76:	//bit 6,(ix+n)
		case 0x77:	BIT_IDX(6);							break;
		case 0x78:
		case 0x79:
		case 0x7A:
		case 0x7B:
		case 0x7C:
		case 0x7D:
		case 0x7E:	//bit 7,(ix+n)
		case 0x7F:	BIT_IDX(7);							break;
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:	//res 0,(ix+n)
		case 0x87:	RES_IDX(0);							break;
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
		case 0x8D:
		case 0x8E:	//res 1,(ix+n)
		case 0x8F:	RES_IDX(1);							break;
		case 0x90:
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:	//res 2,(ix+n)
		case 0x97:	RES_IDX(2);							break;
		case 0x98:
		case 0x99:
		case 0x9A:
		case 0x9B:
		case 0x9C:
		case 0x9D:
		case 0x9E:	//res 3,(ix+n)
		case 0x9F:	RES_IDX(3);							break;
		case 0xA0:
		case 0xA1:
		case 0xA2:
		case 0xA3:
		case 0xA4:
		case 0xA5:
		case 0xA6:	//res 4,(ix+n)
		case 0xA7:	RES_IDX(4);							break;
		case 0xA8:
		case 0xA9:
		case 0xAA:
		case 0xAB:
		case 0xAC:
		case 0xAD:
		case 0xAE:	//res 5,(ix+n)
		case 0xAF:	RES_IDX(5);							break;
		case 0xB0:
		case 0xB1:
		case 0xB2:
		case 0xB3:
		case 0xB4:
		case 0xB5:
		case 0xB6:	//res 6,(ix+n)
		case 0xB7:	RES_IDX(6);							break;
		case 0xB8:
		case 0xB9:
		case 0xBA:
		case 0xBB:
		case 0xBC:
		case 0xBD:
		case 0xBE:	//res 7,(ix+n)
		case 0xBF:	RES_IDX(7);							break;
		case 0xC0:
		case 0xC1:
		case 0xC2:
		case 0xC3:
		case 0xC4:
		case 0xC5:
		case 0xC6:	//set 0,(ix+n)
		case 0xC7:	SET_IDX(0);							break;
		case 0xC8:
		case 0xC9:
		case 0xCA:
		case 0xCB:
		case 0xCC:
		case 0xCD:
		case 0xCE:	//set 1,(ix+n)
		case 0xCF:	SET_IDX(1);							break;
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
		case 0xD4:
		case 0xD5:
		case 0xD6:	//set 2,(ix+n)
		case 0xD7:	SET_IDX(2);							break;
		case 0xD8:
		case 0xD9:
		case 0xDA:
		case 0xDB:
		case 0xDC:
		case 0xDD:
		case 0xDE:	//set 3,(ix+n)
		case 0xDF:	SET_IDX(3);							break;
		case 0xE0:
		case 0xE1:
		case 0xE2:
		case 0xE3:
		case 0xE4:
		case 0xE5:
		case 0xE6:	//set 4,(ix+n)
		case 0xE7:	SET_IDX(4);							break;
		case 0xE8:
		case 0xE9:
		case 0xEA:
		case 0xEB:
		case 0xEC:
		case 0xED:
		case 0xEE:	//set 5,(ix+n)
		case 0xEF:	SET_IDX(5);							break;
		case 0xF0:
		case 0xF1:
		case 0xF2:
		case 0xF3:
		case 0xF4:
		case 0xF5:
		case 0xF6:	//set 6,(ix+n)
		case 0xF7:	SET_IDX(6);							break;
		case 0xF8:
		case 0xF9:
		case 0xFA:
		case 0xFB:
		case 0xFC:
		case 0xFD:
		case 0xFE:	//set 7,(ix+n)
		case 0xFF:	SET_IDX(7);							break;

		default:
			printf("bad DDCB opcode = $%02X\n", opcode);
			break;
	}
}

__inline void step_fd()
{
	unsigned char opcode = read8(PC++);
	unsigned short stmp, tmp16;
	unsigned char tmp;
	unsigned long ltmp, otmp;

	switch (opcode) {
	case 0x09:	ADD16(IY, BC);	CYCLES += 8;	break;
	case 0x19:	ADD16(IY, DE);	CYCLES += 8;	break;
	case 0x21:	//ld IY,nn
		IY = read16(PC);
		PC += 2;
		CYCLES += 14;
		break;
	case 0x22:	//ld (nn),IY
		write16(read16(PC), IY);
		PC += 2;
		CYCLES += 20;
		break;
	case 0x23:	//inc IY
		IY++;
		CYCLES += 10;
		break;
	case 0x24:	//inc iyh
		IY = ((IY + 0x100) & 0xFF00) | (IY & 0xFF);
		CYCLES += 10;
		break;
	case 0x25:	//dec iyh
		IY = ((IY - 0x100) & 0xFF00) | (IY & 0xFF);
		CYCLES += 10;
		break;

	case 0x26:	//ld iyh,n
		IY = (read8(PC++) << 8) | (IY & 0xFF);
		CYCLES += 11;
		break;
	case 0x29:	ADD16(IY, IY);	CYCLES += 8;	break;
	case 0x2A:	//ld IY,(nn)
		IY = read16(read16(PC));
		PC += 2;
		CYCLES += 20;
		break;
	case 0x2B:	//dec IY
		IY--;
		CYCLES += 10;
		break;
	case 0x2C:	//inc iyl
		IY = ((IY & 0xFF00) | ((IY + 1) & 0xFF));
		IY += 0x100;
		CYCLES += 10;
		break;
	case 0x2D:	//dec iyl
		IY = ((IY & 0xFF00) | ((IY - 1) & 0xFF));
		CYCLES += 10;
		break;

	case 0x2E:	//ld iyl,n
		IY = read8(PC++) | (IY & 0xFF00);
		CYCLES += 11;
		break;

	case 0x34:	//inc (IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		tmp = read8(ltmp);
		INC(tmp);
		write8(ltmp, tmp);
		CYCLES += 23;
		break;
	case 0x35:	//dec (IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		tmp = read8(ltmp);
		DEC(tmp);
		write8(ltmp, tmp);
		CYCLES += 23;
		break;

	case 0x36:	//ld (IY+d),n
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, read8(PC++));
		CYCLES += 19;
		break;
	case 0x39:	ADD16(IY, SP);	CYCLES += 8;	break;

	case 0x40:	B = B;								CYCLES += 8;	break;
	case 0x41:	B = C;								CYCLES += 8;	break;
	case 0x42:	B = D;								CYCLES += 8;	break;
	case 0x43:	B = E;								CYCLES += 8;	break;
	case 0x44:	B = IYH;							CYCLES += 8;	break;
	case 0x45:	B = IYL;							CYCLES += 8;	break;
	case 0x46:	//ld b,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		B = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x47:	B = A;								CYCLES += 8;	break;
	case 0x48:	C = B;								CYCLES += 8;	break;
	case 0x49:	C = C;								CYCLES += 8;	break;
	case 0x4A:	C = D;								CYCLES += 8;	break;
	case 0x4B:	C = E;								CYCLES += 8;	break;
	case 0x4C:	C = IYH;							CYCLES += 8;	break;
	case 0x4D:	C = IYL;							CYCLES += 8;	break;
	case 0x4E:	//ld c,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		C = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x4F:	C = A;								CYCLES += 8;	break;
	case 0x50:	D = B;								CYCLES += 8;	break;
	case 0x51:	D = C;								CYCLES += 8;	break;
	case 0x52:	D = D;								CYCLES += 8;	break;
	case 0x53:	D = E;								CYCLES += 8;	break;
	case 0x54:	D = IYH;							CYCLES += 8;	break;
	case 0x55:	D = IYL;							CYCLES += 8;	break;
	case 0x56:	//ld d,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		D = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x57:	D = A;								CYCLES += 8;	break;
	case 0x58:	E = B;								CYCLES += 8;	break;
	case 0x59:	E = C;								CYCLES += 8;	break;
	case 0x5A:	E = D;								CYCLES += 8;	break;
	case 0x5B:	E = E;								CYCLES += 8;	break;
	case 0x5C:	E = IYH;							CYCLES += 8;	break;
	case 0x5D:	E = IYL;							CYCLES += 8;	break;
	case 0x5E:	//ld e,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		E = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x5F:	E = A;								CYCLES += 8;	break;
	case 0x60:	IYH = B;								CYCLES += 8;	break;
	case 0x61:	IYH = C;								CYCLES += 8;	break;
	case 0x62:	IYH = D;								CYCLES += 8;	break;
	case 0x63:	IYH = E;								CYCLES += 8;	break;
	case 0x64:	IYH = IYH;							CYCLES += 8;	break;
	case 0x65:	IYH = IYL;							CYCLES += 8;	break;
	case 0x66:	//ld h,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		H = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x67:	IYH = A;								CYCLES += 8;	break;
	case 0x68:	IYL = B;								CYCLES += 8;	break;
	case 0x69:	IYL = C;								CYCLES += 8;	break;
	case 0x6A:	IYL = D;								CYCLES += 8;	break;
	case 0x6B:	IYL = E;								CYCLES += 8;	break;
	case 0x6C:	IYL = IYH;							CYCLES += 8;	break;
	case 0x6D:	IYL = IYL;							CYCLES += 8;	break;
	case 0x6E:	//ld l,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		L = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x6F:	IYL = A;								CYCLES += 8;	break;

	case 0x70:	//ld (IY+d),b
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, B);
		CYCLES += 19;
		break;
	case 0x71:	//ld (IY+d),c
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, C);
		CYCLES += 19;
		break;
	case 0x72:	//ld (IY+d),d
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, D);
		CYCLES += 19;
		break;
	case 0x73:	//ld (IY+d),e
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, E);
		CYCLES += 19;
		break;
	case 0x74:	//ld (IY+d),h
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, H);
		CYCLES += 19;
		break;
	case 0x75:	//ld (IY+d),l
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, L);
		CYCLES += 19;
		break;
	case 0x77:	//ld (IY+d),a
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		write8(ltmp, A);
		CYCLES += 19;
		break;

	case 0x78:	A = B;								CYCLES += 8;	break;
	case 0x79:	A = C;								CYCLES += 8;	break;
	case 0x7A:	A = D;								CYCLES += 8;	break;
	case 0x7B:	A = E;								CYCLES += 8;	break;
	case 0x7C:	A = IYH;							CYCLES += 8;	break;
	case 0x7D:	A = IYL;							CYCLES += 8;	break;
	case 0x7E:	//ld a,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		A = read8(ltmp);
		CYCLES += 19;
		break;
	case 0x7F:	A = A;								CYCLES += 8;	break;

	case 0x84:	ADD(IYH);							CYCLES += 8;	break;
	case 0x85:	ADD(IYL);							CYCLES += 8;	break;
	case 0x86:	//add a,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		ADD(read8(ltmp));
		CYCLES += 19;
		break;
	case 0x8C:	ADC(IYH);							CYCLES += 8;	break;
	case 0x8D:	ADC(IYL);							CYCLES += 8;	break;
	case 0x8E:	//adc a,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		ADC(read8(ltmp));
		CYCLES += 19;
		break;
	case 0x94:	SUB(IYH);							CYCLES += 8;	break;
	case 0x95:	SUB(IYL);							CYCLES += 8;	break;
	case 0x96:	//sub a,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		SUB(read8(ltmp));
		CYCLES += 19;
		break;
	case 0x9C:	SBC(IYH);							CYCLES += 8;	break;
	case 0x9D:	SBC(IYL);							CYCLES += 8;	break;
	case 0x9E:	//sbc a,(IY+d)
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		SBC(read8(ltmp));
		CYCLES += 19;
		break;
	case 0xA4:	AND(IYH);							CYCLES += 8;	break;
	case 0xA5:	AND(IYL);							CYCLES += 8;	break;
	case 0xA6:
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		tmp = read8(ltmp);
		AND(tmp);
		CYCLES += 19;
		break;
	case 0xAC:	XOR(IYH);							CYCLES += 8;	break;
	case 0xAD:	XOR(IYL);							CYCLES += 8;	break;
	case 0xAE:
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		tmp = read8(ltmp);
		XOR(tmp);
		CYCLES += 19;
		break;
	case 0xB4:	OR(IYH);							CYCLES += 8;	break;
	case 0xB5:	OR(IYL);							CYCLES += 8;	break;
	case 0xB6:
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		tmp = read8(ltmp);
		OR(tmp);
		CYCLES += 19;
		break;
	case 0xBC:	CP(IYH);							CYCLES += 8;	break;
	case 0xBD:	CP(IYL);							CYCLES += 8;	break;
	case 0xBE:
		ltmp = (unsigned long)(unsigned short)((signed short)IY + (signed char)read8(PC++));
		tmp = read8(ltmp);
		CP(tmp);
		CYCLES += 19;
		break;
	case 0xCB:	return(step_fdcb());
	case 0xE1:	//pop IY
		POP16(IY);
		CYCLES += 14;
		break;
	case 0xE5:	//push IY
		PUSH16(IY);
		CYCLES += 15;
		break;

	default:
		printf("bad FD opcode $%02X\n", opcode);
		break;
	}
}

static unsigned char tmp;
static unsigned char tmp2;
static unsigned short stmp, utmp[3];
static unsigned long ltmp, otmp;

void deadz80_step()
{
	register u8 tmp8;

	if (INSIDEIRQ) {
		OPCODE = z80->irqfunc();
		INSIDEIRQ = 0;
	}
	else
		//fetch opcode from memory
		OPCODE = read8(PC++);

	//halt
	if (HALT) {
		return;
	}


	//find what opcode it is and do what it does
	switch(OPCODE) {

	case 0x00:	//nop
		CYCLES += 4;
		break;

	case 0x01:	//ld bc,nnnn
		BC = read16(PC);
		PC += 2;
		CYCLES += 10;
		break;

	case 0x02:	//ld (bc),a
		write8(BC, A);
		CYCLES += 7;
		break;

	case 0x03:	INC16(BC);				CYCLES += 6;	break;
	case 0x04:	INC(B);					CYCLES += 4;	break;
	case 0x05:	DEC(B);					CYCLES += 4;	break;
	case 0x06:	B = read8(PC++);		CYCLES += 7;	break;





	case 0x07:	//rlca
		tmp = (A & 0x80) >> 7;
		A <<= 1;
		A |= tmp & 1;
		F &= ~(FLAG_N | FLAG_H | FLAG_C | 0x28);
		F |= tmp | (A & 0x28);
		CYCLES += 4;
		break;

	case 0x08:	//ex af,af'
		utmp[0] = z80->regs->af.w;
		z80->regs->af.w = z80->alt.af.w;
		z80->alt.af.w = utmp[0];
		CYCLES += 4;
		break;

	case 0x09:	//add hl,bc
		ADD16(HL, BC);
		CYCLES += 4;
		break;

	case 0x0A:	//ld a,(bc)
		A = read8(BC);
		CYCLES += 7;
		break;

	case 0x0B:	DEC16(BC);				CYCLES += 6;	break;
	case 0x0C:	INC(C);					CYCLES += 4;	break;
	case 0x0D:	DEC(C);					CYCLES += 4;	break;
	case 0x0E:	C = read8(PC++);		CYCLES += 7;	break;

	case 0x0F:	//rrca
		F = (F & (FLAG_P | FLAG_Z | FLAG_S)) | (A & 1);
		A = (A >> 1) | ((A << 7) & 0x80);
		F |= A & 0x28;
		CYCLES += 4;
		break;

	case 0x10:	//djnz imm8
		stmp = (signed char)read8(PC++) + PC;
		if (--B > 0) {
			PC = stmp;
			CYCLES += 13;
		}
		else
			CYCLES += 8;
		break;

	case 0x11:	//ld de,imm16
		DE = read16(PC);
		PC += 2;
		CYCLES += 10;
		break;

	case 0x12:	//ld (de),a
		write8(DE, A);
		CYCLES += 7;
		break;

	case 0x13:	INC16(DE);				CYCLES += 6;	break;
	case 0x14:	INC(D);					CYCLES += 4;	break;
	case 0x15:	DEC(D);					CYCLES += 4;	break;
	case 0x16:	D = read8(PC++);		CYCLES += 7;	break;
	case 0x17:	RLA();					CYCLES += 4;	break;

	case 0x18:	//jr simm8
		stmp = (signed char)read8(PC++) + PC;
		PC = stmp;
		CYCLES += 13;
		break;

	case 0x19:	//add hl,de
		ADD16(HL, DE);
		CYCLES += 4;
		break;

	case 0x1A:	A = read8(DE);	CYCLES += 7;	break;
	case 0x1B:	DEC16(DE);				CYCLES += 6;	break;
	case 0x1C:	INC(E);					CYCLES += 4;	break;
	case 0x1D:	DEC(E);					CYCLES += 4;	break;
	case 0x1E:	E = read8(PC++);	CYCLES += 7;	break;

	case 0x1F:	//rra
		tmp = A & 1;
		A >>= 1;
		A |= (F << 7) & 0x80;
		F &= ~(FLAG_N | FLAG_H | FLAG_C | 0x28);
		F |= tmp | (A & 0x28);
		CYCLES += 4;
		break;

	case 0x20:	//jr nz,simm8
		stmp = (signed char)read8(PC++) + PC;
		if ((F & FLAG_Z) == 0) {
			PC = stmp;
			CYCLES += 13;
		}
		else
			CYCLES += 8;
		break;

	case 0x21:	//ld hl,imm16
		HL = read16(PC);
		PC += 2;
		CYCLES += 10;
		break;

	case 0x22:	//ld (u16),hl
		write16(read16(PC), HL);
		PC += 2;
		CYCLES += 16;
		break;

	case 0x23:	INC16(HL);				CYCLES += 6;	break;
	case 0x24:	INC(H);					CYCLES += 4;	break;
	case 0x25:	DEC(H);					CYCLES += 4;	break;
	case 0x26:	H = read8(PC++);		CYCLES += 7;	break;

	case 0x27:	//daa
		//		DAA;

	{
		int     a, c, d;
		a = A;

		if (a > 0x99 || (F & FLAG_C)) {
			c = FLAG_C;
			d = 0x60;
		}
		else
			c = d = 0;

		if ((a & 0x0f) > 0x09 || (F & FLAG_H))
			d += 0x06;

		A += (F & FLAG_N) ? -d : +d;

		F = (F & FLAG_N) | (A & 0x28) | c;
		F |= (A ^ a) & FLAG_H;
		if (A == 0)
			F |= FLAG_Z;
		if (A & 0x80)
			F |= FLAG_S;
		if (parity[A])
			F |= FLAG_P;
	}
		CYCLES += 4;
		break;

	case 0x28:	//jr z,simm8
		stmp = (signed char)read8(PC++) + PC;
		if ((F & FLAG_Z) != 0) {
			PC = stmp;
			CYCLES += 13;
		}
		else
			CYCLES += 8;
		break;

	case 0x29:	ADD16(HL, HL);		CYCLES += 4;	break;

	case 0x2A:	//ld hl,(imm16)
		HL = read16(read16(PC));
		PC += 2;
		CYCLES += 16;
		break;

	case 0x2B:	DEC16(HL);			CYCLES += 6;	break;
	case 0x2C:	INC(L);				CYCLES += 4;	break;
	case 0x2D:	DEC(L);				CYCLES += 4;	break;
	case 0x2E:	L = read8(PC++);	CYCLES += 7;	break;

	case 0x2F:	//cpl
		A ^= 0xFF;
		F &= ~(FLAG_N | FLAG_H | 0x28);
		F |= FLAG_N | FLAG_H | (A & 0x28);
		CYCLES += 4;
		break;

	case 0x30:	//jr nc,simm8
		stmp = (signed char)read8(PC++) + PC;
		if ((F & FLAG_C) == 0) {
			PC = stmp;
			CYCLES += 13;
		}
		else
			CYCLES += 8;
		break;

	case 0x31:	//ld SP,nnnn
		SP = read16(PC);
		PC += 2;
		CYCLES += 10;
		break;

	case 0x32:	//ld (u16),a
		write8(read16(PC), A);
		PC += 2;
		CYCLES += 13;
		break;

	case 0x33:	INC16(SP);		CYCLES += 6;		break;

	case 0x34:	//inc (hl)
		tmp = read8(HL);
		INC(tmp);
		write8(HL, tmp);
		CYCLES += 11;
		break;

	case 0x35:	//dec (hl)
		tmp = read8(HL);
		DEC(tmp);
		write8(HL, tmp);
		CYCLES += 11;
		break;

	case 0x36:	//ld (hl),n
		write8(HL, read8(PC++));
		CYCLES += 10;
		break;

	case 0x37:
		F &= ~(FLAG_H | FLAG_N | 0x28);
		F |= FLAG_C | (A & 0x28);
		CYCLES += 4;
		break;

	case 0x38:	//jr c,simm8
		stmp = (signed char)read8(PC++) + PC;
		if ((F & FLAG_C) != 0) {
			PC = stmp;
			CYCLES += 13;
		}
		else
			CYCLES += 8;
		break;

	case 0x39:	//add hl,SP
		ADD16(HL, SP);
		CYCLES += 4;
		break;

	case 0x3A:	//ld a,(nn)
		A = read8(read16(PC));
		PC += 2;
		CYCLES += 13;
		break;

	case 0x3B:	DEC16(SP);							CYCLES += 6;	break;
	case 0x3C:	INC(A);								CYCLES += 4;	break;
	case 0x3D:	DEC(A);								CYCLES += 4;	break;
	case 0x3E:	A = read8(PC++);					CYCLES += 7;	break;

	case 0x3F:	//ccf
		tmp = F & FLAG_C;
		F &= FLAG_V | FLAG_P | FLAG_Z | FLAG_S;
		F |= (A & 0x28) | (tmp << 4) | (tmp ^ FLAG_C);
		CYCLES += 4;
		break;

	case 0x40:	B = B;								CYCLES += 4;	break;
	case 0x41:	B = C;								CYCLES += 4;	break;
	case 0x42:	B = D;								CYCLES += 4;	break;
	case 0x43:	B = E;								CYCLES += 4;	break;
	case 0x44:	B = H;								CYCLES += 4;	break;
	case 0x45:	B = L;								CYCLES += 4;	break;
	case 0x46:	B = read8(HL);						CYCLES += 7;	break;
	case 0x47:	B = A;								CYCLES += 4;	break;
	case 0x48:	C = B;								CYCLES += 4;	break;
	case 0x49:	C = C;								CYCLES += 4;	break;
	case 0x4A:	C = D;								CYCLES += 4;	break;
	case 0x4B:	C = E;								CYCLES += 4;	break;
	case 0x4C:	C = H;								CYCLES += 4;	break;
	case 0x4D:	C = L;								CYCLES += 4;	break;
	case 0x4E:	C = read8(HL);						CYCLES += 7;	break;
	case 0x4F:	C = A;								CYCLES += 4;	break;
	case 0x50:	D = B;								CYCLES += 4;	break;
	case 0x51:	D = C;								CYCLES += 4;	break;
	case 0x52:	D = D;								CYCLES += 4;	break;
	case 0x53:	D = E;								CYCLES += 4;	break;
	case 0x54:	D = H;								CYCLES += 4;	break;
	case 0x55:	D = L;								CYCLES += 4;	break;
	case 0x56:	D = read8(HL);						CYCLES += 7;	break;
	case 0x57:	D = A;								CYCLES += 4;	break;
	case 0x58:	E = B;								CYCLES += 4;	break;
	case 0x59:	E = C;								CYCLES += 4;	break;
	case 0x5A:	E = D;								CYCLES += 4;	break;
	case 0x5B:	E = E;								CYCLES += 4;	break;
	case 0x5C:	E = H;								CYCLES += 4;	break;
	case 0x5D:	E = L;								CYCLES += 4;	break;
	case 0x5E:	E = read8(HL);						CYCLES += 7;	break;
	case 0x5F:	E = A;								CYCLES += 4;	break;
	case 0x60:	H = B;								CYCLES += 4;	break;
	case 0x61:	H = C;								CYCLES += 4;	break;
	case 0x62:	H = D;								CYCLES += 4;	break;
	case 0x63:	H = E;								CYCLES += 4;	break;
	case 0x64:	H = H;								CYCLES += 4;	break;
	case 0x65:	H = L;								CYCLES += 4;	break;
	case 0x66:	H = read8(HL);						CYCLES += 7;	break;
	case 0x67:	H = A;								CYCLES += 4;	break;
	case 0x68:	L = B;								CYCLES += 4;	break;
	case 0x69:	L = C;								CYCLES += 4;	break;
	case 0x6A:	L = D;								CYCLES += 4;	break;
	case 0x6B:	L = E;								CYCLES += 4;	break;
	case 0x6C:	L = H;								CYCLES += 4;	break;
	case 0x6D:	L = L;								CYCLES += 4;	break;
	case 0x6E:	L = read8(HL);						CYCLES += 7;	break;
	case 0x6F:	L = A;								CYCLES += 4;	break;
	case 0x70:	write8(HL, B);						CYCLES += 7;	break;
	case 0x71:	write8(HL, C);						CYCLES += 7;	break;
	case 0x72:	write8(HL, D);						CYCLES += 7;	break;
	case 0x73:	write8(HL, E);						CYCLES += 7;	break;
	case 0x74:	write8(HL, H);						CYCLES += 7;	break;
	case 0x75:	write8(HL, L);						CYCLES += 7;	break;
	case 0x76:	HALT = 1;	PC--;					CYCLES += 2;	break;
	case 0x77:	write8(HL, A);						CYCLES += 7;	break;
	case 0x78:	A = B;								CYCLES += 4;	break;
	case 0x79:	A = C;								CYCLES += 4;	break;
	case 0x7A:	A = D;								CYCLES += 4;	break;
	case 0x7B:	A = E;								CYCLES += 4;	break;
	case 0x7C:	A = H;								CYCLES += 4;	break;
	case 0x7D:	A = L;								CYCLES += 4;	break;
	case 0x7E:	A = read8(HL);						CYCLES += 7;	break;
	case 0x7F:	A = A;								CYCLES += 4;	break;
	case 0x80:	ADD(B);								CYCLES += 4;	break;
	case 0x81:	ADD(C);								CYCLES += 4;	break;
	case 0x82:	ADD(D);								CYCLES += 4;	break;
	case 0x83:	ADD(E);								CYCLES += 4;	break;
	case 0x84:	ADD(H);								CYCLES += 4;	break;
	case 0x85:	ADD(L);								CYCLES += 4;	break;
	case 0x86:	tmp = read8(HL); ADD(tmp);		CYCLES += 7;	break;
	case 0x87:	ADD(A);								CYCLES += 4;	break;
	case 0x88:	ADC(B);								CYCLES += 4;	break;
	case 0x89:	ADC(C);								CYCLES += 4;	break;
	case 0x8A:	ADC(D);								CYCLES += 4;	break;
	case 0x8B:	ADC(E);								CYCLES += 4;	break;
	case 0x8C:	ADC(H);								CYCLES += 4;	break;
	case 0x8D:	ADC(L);								CYCLES += 4;	break;
	case 0x8E:	tmp = read8(HL); ADC(tmp);		CYCLES += 7;	break;
	case 0x8F:	ADC(A);								CYCLES += 4;	break;
	case 0x90:	SUB(B);								CYCLES += 4;	break;
	case 0x91:	SUB(C);								CYCLES += 4;	break;
	case 0x92:	SUB(D);								CYCLES += 4;	break;
	case 0x93:	SUB(E);								CYCLES += 4;	break;
	case 0x94:	SUB(H);								CYCLES += 4;	break;
	case 0x95:	SUB(L);								CYCLES += 4;	break;
	case 0x96:	tmp = read8(HL); SUB(tmp);		CYCLES += 7;	break;
	case 0x97:	SUB(A);								CYCLES += 4;	break;
	case 0x98:	SBC(B);								CYCLES += 4;	break;
	case 0x99:	SBC(C);								CYCLES += 4;	break;
	case 0x9A:	SBC(D);								CYCLES += 4;	break;
	case 0x9B:	SBC(E);								CYCLES += 4;	break;
	case 0x9C:	SBC(H);								CYCLES += 4;	break;
	case 0x9D:	SBC(L);								CYCLES += 4;	break;
	case 0x9E:	tmp = read8(HL); SBC(tmp);		CYCLES += 7;	break;
	case 0x9F:	SBC(A);								CYCLES += 4;	break;
	case 0xA0:	AND(B);								CYCLES += 4;	break;
	case 0xA1:	AND(C);								CYCLES += 4;	break;
	case 0xA2:	AND(D);								CYCLES += 4;	break;
	case 0xA3:	AND(E);								CYCLES += 4;	break;
	case 0xA4:	AND(H);								CYCLES += 4;	break;
	case 0xA5:	AND(L);								CYCLES += 4;	break;
	case 0xA6:	tmp = read8(HL); AND(tmp);		CYCLES += 7;	break;
	case 0xA7:	AND(A);								CYCLES += 4;	break;
	case 0xA8:	XOR(B);								CYCLES += 4;	break;
	case 0xA9:	XOR(C);								CYCLES += 4;	break;
	case 0xAA:	XOR(D);								CYCLES += 4;	break;
	case 0xAB:	XOR(E);								CYCLES += 4;	break;
	case 0xAC:	XOR(H);								CYCLES += 4;	break;
	case 0xAD:	XOR(L);								CYCLES += 4;	break;
	case 0xAE:	tmp = read8(HL); XOR(tmp);		CYCLES += 7;	break;
	case 0xAF:	XOR(A);								CYCLES += 4;	break;
	case 0xB0:	OR(B);								CYCLES += 4;	break;
	case 0xB1:	OR(C);								CYCLES += 4;	break;
	case 0xB2:	OR(D);								CYCLES += 4;	break;
	case 0xB3:	OR(E);								CYCLES += 4;	break;
	case 0xB4:	OR(H);								CYCLES += 4;	break;
	case 0xB5:	OR(L);								CYCLES += 4;	break;
	case 0xB6:	tmp = read8(HL); OR(tmp);		CYCLES += 7;	break;
	case 0xB7:	OR(A);								CYCLES += 4;	break;
	case 0xB8:	CP(B);								CYCLES += 4;	break;
	case 0xB9:	CP(C);								CYCLES += 4;	break;
	case 0xBA:	CP(D);								CYCLES += 4;	break;
	case 0xBB:	CP(E);								CYCLES += 4;	break;
	case 0xBC:	CP(H);								CYCLES += 4;	break;
	case 0xBD:	CP(L);								CYCLES += 4;	break;
	case 0xBE:	tmp = read8(HL); CP(tmp);		CYCLES += 7;	break;
	case 0xBF:	CP(A);								CYCLES += 4;	break;
	case 0xC0:	RET((F & FLAG_Z) == 0);								break;
	case 0xC1:	POP16(BC);							CYCLES += 10;	break;
	case 0xC2:	JR((F & FLAG_Z) == 0);								break;
	case 0xC3:	JP(read16(PC));					CYCLES += 0;	break;
	case 0xC4:	CALL((F & FLAG_Z) == 0);							break;
	case 0xC5:	PUSH16(BC);							CYCLES += 11;	break;
	case 0xC6:	tmp = read8(PC++); ADD(tmp);	CYCLES += 7;	break;
	case 0xC7:	RST(0x00);												break;
	case 0xC8:	RET((F & FLAG_Z) != 0);								break;
	case 0xC9:	RET(1);	CYCLES -= 1;								break;
	case 0xCA:	JR((F & FLAG_Z) != 0);								break;
	case 0xCB:	step_cb();												break;
	case 0xCC:	CALL((F & FLAG_Z) != 0);							break;
	case 0xCD:	CALL(1);													break;
	case 0xCE:	tmp = read8(PC++); ADC(tmp);	CYCLES += 7;	break;
	case 0xCF:	RST(0x08);												break;
	case 0xD0:	RET((F & FLAG_C) == 0);								break;
	case 0xD1:	POP16(DE);							CYCLES += 10;	break;
	case 0xD2:	JR((F & FLAG_C) == 0);								break;
	case 0xD3:	
		tmp = read8(PC++); 
		OP_OUT(tmp, A, A);
		CYCLES += 11;
		break;
	case 0xD4:	CALL((F & FLAG_C) == 0);							break;
	case 0xD5:	PUSH16(DE);							CYCLES += 11;	break;
	case 0xD6:	SUB(read8(PC++));					CYCLES += 7;	break;
	case 0xD7:	RST(0x10);												break;
	case 0xD8:	RET((F & FLAG_C) != 0);								break;
	case 0xD9:	EXX();													break;
	case 0xDA:	JR((F & FLAG_C) != 0);								break;
	case 0xDB:	//in a,(n)
		tmp8 = deadz80_memread(PC++);
		A = deadz80_ioread(tmp8 | (A << 8));
		CYCLES += 11;
		break;
	case 0xDC:	CALL((F & FLAG_C) != 0);							break;
	case 0xDD:	step_dd();												break;
	case 0xDE:	tmp2 = read8(PC++); SBC(tmp2); CYCLES += 7;	break;
	case 0xDF:	RST(0x18);												break;
	case 0xE0:	RET((F & FLAG_P) == 0);								break;
	case 0xE1:	POP16(HL);							CYCLES += 10;	break;
	case 0xE2:	JR((F & FLAG_P) == 0);								break;

	case 0xE4:	CALL((F & FLAG_P) == 0);							break;
	case 0xE5:	PUSH16(HL);							CYCLES += 11;	break;
	case 0xE6:	AND(read8(PC++));					CYCLES += 7;	break;
	case 0xE7:	RST(0x20);												break;
	case 0xE8:	RET((F & FLAG_P) != 0);								break;
	case 0xE9:	JP(HL);													break;
	case 0xEA:	JR((F & FLAG_P) != 0);								break;
	case 0xEB:	EX(DE, HL);												break;
	case 0xEC:	CALL((F & FLAG_P) != 0);							break;
	case 0xED:	step_ed();												break;
	case 0xEE:	XOR(read8(PC++));					CYCLES += 7;	break;
	case 0xEF:	RST(0x28);												break;
	case 0xF0:	RET((F & FLAG_S) == 0);								break;
	case 0xF1:	POP16(AF);							CYCLES += 10;	break;
	case 0xF2:	JR((F & FLAG_S) == 0);								break;
	case 0xF3:	IFF1 = IFF2 = 0;					CYCLES += 4;	break;
	case 0xF4:	CALL((F & FLAG_S) == 0);							break;
	case 0xF5:	PUSH16(AF);							CYCLES += 11;	break;
	case 0xF6:	OR(read8(PC++));					CYCLES += 7;	break;
	case 0xF7:	RST(0x30);												break;
	case 0xF8:	RET((F & FLAG_S) != 0);								break;
	case 0xF9:	SP = HL;								CYCLES += 6;	break;
	case 0xFA:	JR((F & FLAG_S) != 0);								break;
	case 0xFB:	IFF1 = IFF2 = 1;					CYCLES += 4;	break;
	case 0xFC:	CALL((F & FLAG_S) != 0);							break;
	case 0xFD:	step_fd();												break;
	case 0xFE:	tmp = read8(PC++); CP(tmp);	CYCLES += 7;	break;
	case 0xFF:	RST(0x38);												break;

	default:	//bad oPCode
		printf("bad opcode = $%02X\n", OPCODE);
		return;

	}
}

u32 deadz80_execute(u32 cycles)
{
	u32 c, oldc, total = 0;

	while (total < cycles) {
		oldc = CYCLES;
		deadz80_step();
		c = CYCLES - oldc;
		total += c;
	}
	return(total);
}

static char *op_xx_cb[256] =
{
	"?", "?", "?", "?", "?", "?", "rlc Y", "?",
	"?", "?", "?", "?", "?", "?", "rrc Y", "?",
	"?", "?", "?", "?", "?", "?", "rl Y", "?",
	"?", "?", "?", "?", "?", "?", "rr Y", "?",
	"?", "?", "?", "?", "?", "?", "sla Y", "?",
	"?", "?", "?", "?", "?", "?", "sra Y", "?",
	"?", "?", "?", "?", "?", "?", "sll Y", "?",
	"?", "?", "?", "?", "?", "?", "srl Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 0,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 1,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 2,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 3,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 4,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 5,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 6,Y", "?",
	"?", "?", "?", "?", "?", "?", "bit 7,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 0,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 1,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 2,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 3,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 4,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 5,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 6,Y", "?",
	"?", "?", "?", "?", "?", "?", "res 7,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 0,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 1,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 2,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 3,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 4,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 5,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 6,Y", "?",
	"?", "?", "?", "?", "?", "?", "set 7,Y", "?"
};

static char *op_dd[256] =
{
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "pop ix", "?", "?", "?", "?", "push ix", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
};
static char *op_ddcb[256] =
{
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
};

static char *op_fdcb[256] =
{
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
};

static char *op_fd[256] =
{
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "pop iy", "?", "?", "?", "push iy", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
};

static char *op_cb[256] =
{
	"rlc b", "rlc c", "rlc d", "rlc e", "rlc h", "rlc l", "rlc (hl)", "rlc a",
	"rrc b", "rrc c", "rrc d", "rrc e", "rrc h", "rrc l", "rrc (hl)", "rrc a",
	"rl b", "rl c", "rl d", "rl e", "rl h", "rl l", "rl (hl)", "rl a",
	"rr b", "rr c", "rr d", "rr e", "rr h", "rr l", "rr (hl)", "rr a",
	"sla b", "sla c", "sla d", "sla e", "sla h", "sla l", "sla (hl)", "sla a",
	"sra b", "sra c", "sra d", "sra e", "sra h", "sra l", "sra (hl)", "sra a",
	"sll b", "sll c", "sll d", "sll e", "sll h", "sll l", "sll (hl)", "sll a",
	"srl b", "srl c", "srl d", "srl e", "srl h", "srl l", "srl (hl)", "srl a",
	"bit 0,b", "bit 0,c", "bit 0,d", "bit 0,e", "bit 0,h", "bit 0,l", "bit 0,(hl)", "bit 0,a",
	"bit 1,b", "bit 1,c", "bit 1,d", "bit 1,e", "bit 1,h", "bit 1,l", "bit 1,(hl)", "bit 1,a",
	"bit 2,b", "bit 2,c", "bit 2,d", "bit 2,e", "bit 2,h", "bit 2,l", "bit 2,(hl)", "bit 2,a",
	"bit 3,b", "bit 3,c", "bit 3,d", "bit 3,e", "bit 3,h", "bit 3,l", "bit 3,(hl)", "bit 3,a",
	"bit 4,b", "bit 4,c", "bit 4,d", "bit 4,e", "bit 4,h", "bit 4,l", "bit 4,(hl)", "bit 4,a",
	"bit 5,b", "bit 5,c", "bit 5,d", "bit 5,e", "bit 5,h", "bit 5,l", "bit 5,(hl)", "bit 5,a",
	"bit 6,b", "bit 6,c", "bit 6,d", "bit 6,e", "bit 6,h", "bit 6,l", "bit 6,(hl)", "bit 6,a",
	"bit 7,b", "bit 7,c", "bit 7,d", "bit 7,e", "bit 7,h", "bit 7,l", "bit 7,(hl)", "bit 7,a",
	"res 0,b", "res 0,c", "res 0,d", "res 0,e", "res 0,h", "res 0,l", "res 0,(hl)", "res 0,a",
	"res 1,b", "res 1,c", "res 1,d", "res 1,e", "res 1,h", "res 1,l", "res 1,(hl)", "res 1,a",
	"res 2,b", "res 2,c", "res 2,d", "res 2,e", "res 2,h", "res 2,l", "res 2,(hl)", "res 2,a",
	"res 3,b", "res 3,c", "res 3,d", "res 3,e", "res 3,h", "res 3,l", "res 3,(hl)", "res 3,a",
	"res 4,b", "res 4,c", "res 4,d", "res 4,e", "res 4,h", "res 4,l", "res 4,(hl)", "res 4,a",
	"res 5,b", "res 5,c", "res 5,d", "res 5,e", "res 5,h", "res 5,l", "res 5,(hl)", "res 5,a",
	"res 6,b", "res 6,c", "res 6,d", "res 6,e", "res 6,h", "res 6,l", "res 6,(hl)", "res 6,a",
	"res 7,b", "res 7,c", "res 7,d", "res 7,e", "res 7,h", "res 7,l", "res 7,(hl)", "res 7,a",
	"set 0,b", "set 0,c", "set 0,d", "set 0,e", "set 0,h", "set 0,l", "set 0,(hl)", "set 0,a",
	"set 1,b", "set 1,c", "set 1,d", "set 1,e", "set 1,h", "set 1,l", "set 1,(hl)", "set 1,a",
	"set 2,b", "set 2,c", "set 2,d", "set 2,e", "set 2,h", "set 2,l", "set 2,(hl)", "set 2,a",
	"set 3,b", "set 3,c", "set 3,d", "set 3,e", "set 3,h", "set 3,l", "set 3,(hl)", "set 3,a",
	"set 4,b", "set 4,c", "set 4,d", "set 4,e", "set 4,h", "set 4,l", "set 4,(hl)", "set 4,a",
	"set 5,b", "set 5,c", "set 5,d", "set 5,e", "set 5,h", "set 5,l", "set 5,(hl)", "set 5,a",
	"set 6,b", "set 6,c", "set 6,d", "set 6,e", "set 6,h", "set 6,l", "set 6,(hl)", "set 6,a",
	"set 7,b", "set 7,c", "set 7,d", "set 7,e", "set 7,h", "set 7,l", "set 7,(hl)", "set 7,a"
};

static char *op_ed[256] =
{
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"in b,(c)", "out (c),b", "sbc hl,bc", "ld (W),bc", "neg", "retn", "im 0", "ld i,a",
	"in c,(c)", "out (c),c", "adc hl,bc", "ld bc,(W)", "?", "reti", "?", "ld r,a",
	"in d,(c)", "out (c),d", "sbc hl,de", "ld (W),de", "?", "?", "im 1", "ld a,i",
	"in e,(c)", "out (c),e", "adc hl,de", "ld de,(W)", "?", "?", "im 2", "ld a,r",
	"in h,(c)", "out (c),h", "sbc hl,hl", "ld (W),hl", "?", "?", "?", "rrd",
	"in l,(c)", "out (c),l", "adc hl,hl", "ld hl,(W)", "?", "?", "?", "rld",
	"in 0,(c)", "out (c),0", "sbc hl,sp", "ld (W),sp", "?", "?", "?", "?",
	"in a,(c)", "out (c),a", "adc hl,sp", "ld sp,(W)", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"ldi", "cpi", "ini", "outi", "?", "?", "?", "?",
	"ldd", "cpd", "ind", "outd", "?", "?", "?", "?",
	"ldir", "cpir", "inir", "otir", "?", "?", "?", "?",
	"lddr", "cpdr", "indr", "otdr", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?"
};

static char *op_xx[256] =
{
	"@", "@", "@", "@", "@", "@", "@", "@",
	"@", "add I,bc", "@", "@", "@", "@", "@", "@",
	"@", "@", "@", "@", "@", "@", "@", "@",
	"@", "add I,de", "@", "@", "@", "@", "@", "@",
	"@", "ld I,W", "ld (W),I", "inc I", "inc Ih", "dec Ih", "ld Ih,B", "@",
	"@", "add I,I", "ld I,(W)", "dec I", "inc Il", "dec Il", "ld Il,B", "@",
	"@", "@", "@", "@", "inc X", "dec X", "ld X,B", "@",
	"@", "add I,sp", "@", "@", "@", "@", "@", "@",
	"@", "@", "@", "@", "ld b,Ih", "ld b,Il", "ld b,X", "@",
	"@", "@", "@", "@", "ld c,Ih", "ld c,Il", "ld c,X", "@",
	"@", "@", "@", "@", "ld d,Ih", "ld d,Il", "ld d,X", "@",
	"@", "@", "@", "@", "ld e,Ih", "ld e,Il", "ld e,X", "@",
	"ld Ih,b", "ld Ih,c", "ld Ih,d", "ld Ih,e", "ld Ih,h", "ld Ih,l", "ld h,X", "ld Ih,a",
	"ld Il,b", "ld Il,c", "ld Il,d", "ld Il,e", "ld Il,h", "ld Il,l", "ld l,X", "ld Il,a",
	"ld X,b", "ld X,c", "ld X,d", "ld X,e", "ld X,h", "ld X,l", "@", "ld X,a",
	"@", "@", "@", "@", "ld a,Ih", "ld a,Il", "ld a,X", "@",
	"@", "@", "@", "@", "add a,Ih", "add a,Il", "add a,X", "@",
	"@", "@", "@", "@", "adc a,Ih", "adc a,Il", "adc a,X", "@",
	"@", "@", "@", "@", "sub Ih", "sub Il", "sub X", "@",
	"@", "@", "@", "@", "sbc a,Ih", "sbc a,Il", "sbc a,X", "@",
	"@", "@", "@", "@", "and Ih", "and Il", "and X", "@",
	"@", "@", "@", "@", "xor Ih", "xor Il", "xor X", "@",
	"@", "@", "@", "@", "or Ih", "or Il", "or X", "@",
	"@", "@", "@", "@", "cp Ih", "cp Il", "cp X", "@",
	"@", "@", "@", "@", "@", "@", "@", "@",
	"@", "@", "@", "fd cb", "@", "@", "@", "@",
	"@", "@", "@", "@", "@", "@", "@", "@",
	"@", "@", "@", "@", "@", "@", "@", "@",
	"@", "pop I", "@", "ex (sp),I", "@", "push I", "@", "@",
	"@", "jp (I)", "@", "@", "@", "@", "@", "@",
	"@", "@", "@", "@", "@", "@", "@", "@",
	"@", "ld sp,I", "@", "@", "@", "@", "@", "@"
};

static char *op_main[256] =
{
	"nop", "ld bc,W", "ld (bc),a", "inc bc", "inc b", "dec b", "ld b,B", "rlca",
	"ex af,af'", "add hl,bc", "ld a,(bc)", "dec bc", "inc c", "dec c", "ld c,B", "rrca",
	"djnz R", "ld de,W", "ld (de),a", "inc de", "inc d", "dec d", "ld d,B", "rla",
	"jr R", "add hl,de", "ld a,(de)", "dec de", "inc e", "dec e", "ld e,B", "rra",
	"jr nz,R", "ld hl,W", "ld (W),hl", "inc hl", "inc h", "dec h", "ld h,B", "daa",
	"jr z,R", "add hl,hl", "ld hl,(W)", "dec hl", "inc l", "dec l", "ld l,B", "cpl",
	"jr nc,R", "ld sp,W", "ld (W),a", "inc sp", "inc (hl)", "dec (hl)", "ld (hl),B", "scf",
	"jr c,R", "add hl,sp", "ld a,(W)", "dec sp", "inc a", "dec a", "ld a,B", "ccf",
	"ld b,b", "ld b,c", "ld b,d", "ld b,e", "ld b,h", "ld b,l", "ld b,(hl)", "ld b,a",
	"ld c,b", "ld c,c", "ld c,d", "ld c,e", "ld c,h", "ld c,l", "ld c,(hl)", "ld c,a",
	"ld d,b", "ld d,c", "ld d,d", "ld d,e", "ld d,h", "ld d,l", "ld d,(hl)", "ld d,a",
	"ld e,b", "ld e,c", "ld e,d", "ld e,e", "ld e,h", "ld e,l", "ld e,(hl)", "ld e,a",
	"ld h,b", "ld h,c", "ld h,d", "ld h,e", "ld h,h", "ld h,l", "ld h,(hl)", "ld h,a",
	"ld l,b", "ld l,c", "ld l,d", "ld l,e", "ld l,h", "ld l,l", "ld l,(hl)", "ld l,a",
	"ld (hl),b", "ld (hl),c", "ld (hl),d", "ld (hl),e", "ld (hl),h", "ld (hl),l", "halt", "ld (hl),a",
	"ld a,b", "ld a,c", "ld a,d", "ld a,e", "ld a,h", "ld a,l", "ld a,(hl)", "ld a,a",
	"add a,b", "add a,c", "add a,d", "add a,e", "add a,h", "add a,l", "add a,(hl)", "add a,a",
	"adc a,b", "adc a,c", "adc a,d", "adc a,e", "adc a,h", "adc a,l", "adc a,(hl)", "adc a,a",
	"sub b", "sub c", "sub d", "sub e", "sub h", "sub l", "sub (hl)", "sub a",
	"sbc a,b", "sbc a,c", "sbc a,d", "sbc a,e", "sbc a,h", "sbc a,l", "sbc a,(hl)", "sbc a,a",
	"and b", "and c", "and d", "and e", "and h", "and l", "and (hl)", "and a",
	"xor b", "xor c", "xor d", "xor e", "xor h", "xor l", "xor (hl)", "xor a",
	"or b", "or c", "or d", "or e", "or h", "or l", "or (hl)", "or a",
	"cp b", "cp c", "cp d", "cp e", "cp h", "cp l", "cp (hl)", "cp a",
	"ret nz", "pop bc", "jp nz,W", "jp W", "call nz,W", "push bc", "add a,B", "rst 00h",
	"ret z", "ret", "jp z,W", "cb", "call z,W", "call W", "adc a,B", "rst 08h",
	"ret nc", "pop de", "jp nc,W", "out (B),a", "call nc,W", "push de", "sub B", "rst 10h",
	"ret c", "exx", "jp c,W", "in a,(B)", "call c,W", "dd", "sbc a,B", "rst 18h",
	"ret po", "pop hl", "jp po,W", "ex (sp),hl", "call po,W", "push hl", "and B", "rst 20h",
	"ret pe", "jp (hl)", "jp pe,W", "ex de,hl", "call pe,W", "ed", "xor B", "rst 28h",
	"ret p", "pop af", "jp p,W", "di", "call p,W", "push af", "or B", "rst 30h",
	"ret m", "ld sp,hl", "jp m,W", "ei", "call m,W", "fd", "cp B", "rst 38h"
};

u32 deadz80_disassemble(char *dest, u32 p)
{
	u32 oldpc = p;
	u8 opcode, opcode2, data, data2;
	char *ptr = 0, str[128], tmp[256];
	int n;

	//disasm = line 21
	//regs = line 40
	memset(dest, ' ', 256);
	memset(tmp, ' ', 256);
	dest[255] = 0;
	tmp[255] = 0;

	opcode = deadz80_memread(p++);
	switch (opcode) {
	case 0xCB:
		opcode2 = deadz80_memread(p++);
		ptr = op_cb[opcode2];
		sprintf(dest, "$%04X: %02X %02X", oldpc, opcode, opcode2);
		break;
	case 0xDD:
		opcode2 = deadz80_memread(p++);
		if (opcode2 == 0xCB) {
			opcode = deadz80_memread(p++);
			opcode2 = deadz80_memread(p++);
			ptr = op_ddcb[opcode2];
			sprintf(dest, "$%04X: DD CB %02X %02X", oldpc, opcode, opcode2);
		}
		else {
			ptr = op_dd[opcode2];
			sprintf(dest, "$%04X: %02X %02X", oldpc, opcode, opcode2);
		}
		break;
	case 0xED:
		opcode2 = deadz80_memread(p++);
		ptr = op_ed[opcode2];
		sprintf(dest, "$%04X: %02X %02X", oldpc, opcode, opcode2);
		break;
	case 0xFD:
		opcode2 = deadz80_memread(p++);
		if (opcode2 == 0xCB) {
			opcode = deadz80_memread(p++);
			opcode2 = deadz80_memread(p++);
			ptr = op_fdcb[opcode2];
			sprintf(dest, "$%04X: FD CB %02X %02X", oldpc, opcode, opcode2);
		}
		else {
			ptr = op_fd[opcode2];
			sprintf(dest, "$%04X: %02X %02X", oldpc, opcode, opcode2);
		}
		break;
	default:
		ptr = op_main[opcode];
		sprintf(dest, "$%04X: %02X", oldpc, opcode);
		break;
	}
	if (ptr == 0)
		sprintf(dest, "$%04X: %02X", oldpc, opcode);
	else {
		str[0] = 0;
		while (*ptr) {
			switch (*ptr) {
			case 'R':
				data = deadz80_memread(p++);
				sprintf(tmp, " %02X", data);
				strcat(dest, tmp);
				sprintf(tmp, "$%04X", (p + (signed char)data) & 0xFFFF);
				strcat(str, tmp);
				break;
			case 'B':
				data = deadz80_memread(p++);
				sprintf(tmp, " %02X", data);
				strcat(dest, tmp);
				sprintf(tmp, "$%02X", data);
				strcat(str, tmp);
				break;
			case 'W':
				data = deadz80_memread(p++);
				data2 = deadz80_memread(p++);
				sprintf(tmp, " %02X %02X", data, data2);
				strcat(dest, tmp);
				sprintf(tmp, "$%04X", data | (data2 << 8));
				strcat(str, tmp);
				break;
			default:
				tmp[0] = *ptr;
				tmp[1] = 0;
				strcat(str, tmp);
				break;
			}
			ptr++;
		}
//		if (dest[strlen(dest) - 1] != '\t')
//			strcat(dest, "\t");
		for (n = strlen(dest); n < 21; n++) {
			dest[n] = ' ';
		}
		sprintf(dest + 20, str);
	}
	dest[strlen(dest)] = ' ';
	sprintf(dest + 41, "AF=$%04X BC=$%04X DE=$%04X HL=$%04X SP=$%04X PC=$%04X", z80->regs->af.w, z80->regs->bc.w, z80->regs->de.w, z80->regs->hl.w, z80->sp, z80->pc);
	{
	//	FILE *fp = fopen("cpu.log", "at");
	//	fprintf(fp,"%s\n",dest);
	//	fclose(fp);
	}
	return(p);
}
