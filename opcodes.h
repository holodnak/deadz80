#ifndef __deadz80_opcodes_h__
#define __deadz80_opcodes_h__

#define setH()		F |= FLAG_H;

#define checkSZ(v)					\
	if((v) == 0)					\
		F |= FLAG_Z;			\
	if((v) & 0x80)					\
		F |= FLAG_S;

#define checkP(v)		\
	F &= ~FLAG_P;	\
	F |= parity[v];

#define checkC(v)		\
	F &= ~FLAG_C;	\
	if((v) >= 0x100)	\
		F |= FLAG_C;

#define checkV(v1,v2,r)	\
	F |= ((((v1) ^ (r)) & ((v2) ^ (r))) & 0x80) >> 5;

#define checkV16(v1,v2,r)	\
	F |= ((((v1) ^ (r)) & ((v2) ^ (r))) & 0x8000) >> (5 + 8);

#define checkSZ16(v)				\
	if((v) == 0)					\
		F |= FLAG_Z;			\
	if((v) & 0x8000)				\
		F |= FLAG_S;

#define checkHV16(v1,v2,r)				\
	if((((v1) & 0xFFF) + ((v2) & 0xFFF)) >= 0x1000)		\
		F |= FLAG_H;				\
	checkV16(v1,v2,r);

#define checkC16(v)			\
	if((v) >= 0x10000)		\
		F |= FLAG_C;

//some opcode helper macros
#define OP_OUT(l,h,v)	\
	z80->iowritefunc(l | (h << 8),v);



#define ADD(v)					\
	stmp = A + v;			\
	F &= ~(FLAG_S | FLAG_Z | FLAG_C | FLAG_V | FLAG_N | FLAG_H);			\
	F |= (stmp) & 0x28;				\
	F = stmp & 0x28;	\
	checkSZ(stmp & 0xFF);		\
	checkC(stmp);				\
	checkV(A,v,stmp);		\
	if(((A & 0xF) + (v & 0xF)) >= 0x10)	\
		F |= FLAG_H;		\
	A = (u8)stmp;

#define ADC(v)					\
	stmp = A + v + (F & 1);	\
	otmp = (A & 0xF) + (v & 0xF) + (F & 1); \
	F = stmp & 0x28;			\
	if((otmp) >= 0x10)	\
		F |= FLAG_H;		\
	checkSZ(stmp & 0xFF);		\
	checkC(stmp);				\
	checkV(A,v,stmp);		\
	A = (u8)stmp;

#define ADC16(v1,v2)					\
	ltmp = v1 + v2 + (F & 1);	\
	F = (ltmp >> 8) & 0x28;				\
	checkSZ16(ltmp & 0xFFFF);		\
	checkC16(ltmp);				\
	checkV16(v1,v2,ltmp);		\
	if(((v1 & 0xFFF) + (v2 & 0xFFF)) >= 0x1000)	\
		F |= FLAG_H;		\
	v1 = (u16)ltmp;

#define AND(v)		\
	A &= v;			\
	F = A & 0x28;	\
	setH();			\
	checkSZ(A);		\
	checkP(A);

#define XOR(v)		\
	A ^= v;			\
	F = A & 0x28;	\
	checkSZ(A);		\
	checkP(A);

#define OR(v)		\
	A |= v;			\
	F = A & 0x28;	\
	checkSZ(A);		\
	checkP(A);

#define DEC(r)							\
	stmp = r - 1;						\
	F &= ~(FLAG_S | FLAG_Z | FLAG_V | FLAG_H | 0x28);	\
	F |= FLAG_N | (stmp & 0x28);		\
	checkSZ(stmp & 0xFF);				\
	checkV(stmp,1,r);					\
	if((r & 0xF) < (1 & 0xF))			\
		F |= FLAG_H;				\
	r = (u8)stmp;

#define INC(r)	\
	stmp = r + 1;			\
	F &= ~(FLAG_S | FLAG_Z | FLAG_V | FLAG_N | FLAG_H | 0x28);	\
	F |= stmp & 0x28;		\
	checkSZ(stmp & 0xFF);	\
	checkV(r,1,stmp);		\
	if((r & 0xF) == 0xF) \
		F |= FLAG_H;		\
	r = (u8)stmp;

#define CP(n)	\
	stmp = A - n;				\
	F &= ~(FLAG_S | FLAG_Z | FLAG_C | FLAG_V | FLAG_N | FLAG_H);	\
	F = (n & 0x28) | FLAG_N;				\
	checkSZ(stmp & 0xFF);			\
	checkC(stmp);					\
	checkV(stmp,n,A);			\
	if((A & 0xF) < (n & 0xF))		\
		F |= FLAG_H;

#define BIT(b,r)			\
tmp = (1 << b) & r;		\
F &= FLAG_C; \
F |= (r & 0x28) | FLAG_H;		\
F |= (tmp ? 0 : FLAG_Z | FLAG_P) | (tmp & FLAG_S);

#define BIT_HL(b,r)			\
tmp = (1 << b) & r;		\
F &= FLAG_C; \
F |= (HL & 0x28) | FLAG_H;		\
F |= (tmp ? 0 : FLAG_Z | FLAG_P) | (tmp & FLAG_S);

#define SET(b,r)			\
	r = (1 << b) | r;

#define RES(b,r)			\
	r = r & ~(1 << b);

#define SUB(n)						\
	tmp = n;						\
	stmp = A - tmp;					\
	F &= ~(FLAG_S | FLAG_Z | FLAG_C | FLAG_V | FLAG_H | 0x28);	\
	F |= stmp & 0x28;	\
	checkSZ(stmp & 0xFF);			\
	checkC(stmp);					\
	checkV(stmp,tmp,A);				\
	if((A & 0xF) < (tmp & 0xF))		\
	F |= FLAG_H;				\
	A = (u8)stmp;						\
	F |= FLAG_N;

#define SBC(n)						\
	tmp = n;						\
	stmp = A - tmp - (F & 1);		\
	F = FLAG_N | ((A ^ (n) ^ stmp) & FLAG_H);		\
	F |= stmp & 0x28;				\
	checkSZ(stmp & 0xFF);			\
	checkC(stmp);					\
	checkV(stmp,tmp,A);				\
	A = (u8)stmp;					\

#define SBC16(n1,n2)				\
	stmp = n2;						\
	ltmp = n1 - stmp - (F & 1);		\
	F &= ~(FLAG_S | FLAG_Z | FLAG_C | FLAG_V | FLAG_H | 0x28);	\
	F |= (ltmp >> 8) & 0x28;				\
	checkSZ16(ltmp & 0xFFFF);			\
	checkC16(ltmp);					\
	checkV16(ltmp,stmp,n1);				\
	if((n1 & 0xFFF) < (ltmp & 0xFFF))	\
		F |= FLAG_H;				\
	n1 = (u16)ltmp;					\
	F |= FLAG_N;

#define RET(c)			\
	if(c) {				\
		PC = read8(SP+0) | (read8(SP+1) << 8);	\
		SP += 2;		\
		CYCLES += 11;	\
		}					\
		else				\
		CYCLES += 5;

#define RST(v)		\
	write8(--SP,(PC >> 8) & 0xFF);	\
	write8(--SP,(PC >> 0) & 0xFF);	\
	PC = v;			\
	CYCLES += 13;

#define JP(r)	\
	PC = r;	\
	CYCLES += 10;

#define JR(c)	\
	stmp = read16(PC);	\
	PC += 2;	\
	if(c) {		\
		PC = stmp;		\
		CYCLES += 10;	\
		}		\
	else	\
		CYCLES += 10;

#define CALL(c)		\
	if(c) {			\
		stmp = PC + 2;	\
		write8(--SP,(stmp >> 8) & 0xFF);		\
		write8(--SP,stmp & 0xFF);				\
		PC = read16(PC);	\
		CYCLES += 17;			\
		}		\
		else {	\
		PC += 2;		\
		CYCLES += 10;	\
		}

#define DEC16(r)	\
	r--;

#define INC16(r)	\
	r++;

#define ADD16(a1,a2)	\
	ltmp = a1 + a2;				\
	F &= ~(FLAG_C | FLAG_N | FLAG_H | 0x28);	\
	F |= (ltmp >> 8) & 0x28;					\
	checkC16(ltmp);								\
	if(((a1 & 0xFFF) + (a2 & 0xFFF)) >= 0x1000)	\
		F |= FLAG_H;					\
	a1 = (u16)ltmp;					\
	CYCLES += 7;

#define PUSH(v)	\
	write8(--SP,v)

#define POP()	\
	read8(SP++)

#define PUSH16(rr)	\
	write8(--SP,((rr) >> 8) & 0xFF);	\
	write8(--SP,(rr) & 0xFF);

#define POP16(rr)	\
	rr = read16(SP);	\
	SP += 2;

#define EXX()	\
	utmp[0] = z80->regs->bc.w;	\
	utmp[1] = z80->regs->de.w;	\
	utmp[2] = z80->regs->hl.w;	\
	z80->regs->bc.w = z80->alt.bc.w;	\
	z80->regs->de.w = z80->alt.de.w;	\
	z80->regs->hl.w = z80->alt.hl.w;	\
	z80->alt.bc.w = utmp[0];	\
	z80->alt.de.w = utmp[1];	\
	z80->alt.hl.w = utmp[2];	\
	CYCLES += 4;

#define EX(r1,r2)	\
	stmp = r1;		\
	r1 = r2;		\
	r2 = stmp;		\
	CYCLES += 4;

#define RLC(d)						\
	d = (d << 1) | (d >> 7);		\
	F = (d & FLAG_C) | (d & 0x28);	\
	checkSZ(d);						\
	checkP(d);

#define RRC(d)					\
	F = d & FLAG_C;				\
	d = (d >> 1) | (d << 7);	\
	F |= d & 0x28;				\
	checkSZ(d);					\
	checkP(d);

#define RLA()					\
	tmp = (A >> 7) & 1;		\
	A = (A << 1) | (F & FLAG_C);\
	F &= (FLAG_S | FLAG_Z | FLAG_P);		\
	F |= tmp | (A & 0x28);

#define RL(d)					\
	tmp = (d >> 7) & 1;		\
	d = (d << 1) | (F & FLAG_C);\
	F = (d & 0x28) | tmp;		\
	checkSZ(d);					\
	checkP(d);

#define RR(d)					\
	tmp = d & 1;				\
	d = (d >> 1) | ((F & FLAG_C) << 7);\
	F = (d & 0x28) | tmp;		\
	checkSZ(d);					\
	checkP(d);

#define SLA(d)					\
	F = d >> 7;					\
	d = d << 1;					\
	F |= (d & 0x28);			\
	checkSZ(d);					\
	checkP(d);

#define SRA(d)					\
	F = d & 1;					\
	d = (d >> 1) | (d & 0x80);	\
	F |= (d & 0x28);			\
	checkSZ(d);					\
	checkP(d);

#define SLL(d)					\
	F = d >> 7;					\
	d = (d << 1) | 1;			\
	F |= (d & 0x28);			\
	checkSZ(d);					\
	checkP(d);

#define SRL(d)					\
	F = d & 1;					\
	d = d >> 1;					\
	F |= (d & 0x28);			\
	checkSZ(d);					\
	checkP(d);

#define BIT_IDX(b)	\
tmp = (1 << b) & read8(ltmp);		\
F &= FLAG_C; \
F |= ((ltmp >> 8) & 0x28) | FLAG_H;		\
F |= (tmp ? 0 : FLAG_Z | FLAG_P) | (tmp & FLAG_S); \
	CYCLES += 20;

#define SET_IDX(b)	\
	tmp = read8(ltmp);	\
	SET(b,tmp);					\
	write8(ltmp,tmp);		\
	CYCLES += 19;

#define RES_IDX(b)	\
	tmp = read8(ltmp);	\
	RES(b,tmp);					\
	write8(ltmp,tmp);		\
	CYCLES += 19;

#endif