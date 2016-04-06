#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deadz80.h"
#include "z80emu/z80emu.h"

#define MAXIMUM_STRING_LENGTH   100

u8 mem[0x10000];
u8 mem2[0x10000];
deadz80_t *z80;

static u8 ioread(u32 addr)
{
//	printf("ioread $%04X\n", addr);

	if (z80->regs->bc.b.c == 2) {
		printf("%c", z80->regs->de.b.e);
	}

	else if (z80->regs->bc.b.c == 9) {
		int i, c;

		for (i = z80->regs->de.w, c = 0; mem[i] != '$'; i++) {
			printf("%c", mem[i & 0xffff]);
			if (c++ > MAXIMUM_STRING_LENGTH) {

				fprintf(stderr,
					"String to print is too long!\n");
				exit(EXIT_FAILURE);

			}

		}

	}

	return(0);
}

static void iowrite(u32 addr, u8 data)
{
//	printf("iowrite $%04X = $%02X\n", addr, data);

}

extern unsigned char memory[];

int test2(void);

int state_save(char *filename)
{
	FILE *fp;

	if ((fp = fopen(filename, "wb")) == 0) {
		printf("error opening\n");
		return(1);
	}
#define write_u8(v, f) fwrite(&v,1,1,f)
#define write_u16(v, f) fwrite(&v,2,1,f)
#define write_u32(v, f) fwrite(&v,4,1,f)
#define read_u8(v, f) fread(&v,1,1,f)
#define read_u16(v, f) fread(&v,2,1,f)
#define read_u32(v, f) fread(&v,4,1,f)
	write_u16(z80->main.af.w, fp);
	write_u16(z80->main.bc.w, fp);
	write_u16(z80->main.de.w, fp);
	write_u16(z80->main.hl.w, fp);
	write_u16(z80->alt.af.w, fp);
	write_u16(z80->alt.bc.w, fp);
	write_u16(z80->alt.de.w, fp);
	write_u16(z80->alt.hl.w, fp);
	write_u16(z80->pc, fp);
	write_u16(z80->sp, fp);
	write_u16(z80->ix.w, fp);
	write_u16(z80->iy.w, fp);
	write_u32(z80->cycles, fp);
	fwrite(mem, 0x10000, 1, fp);
	fclose(fp);
	return(0);
}

int state_load(char *filename)
{
	FILE *fp;

	if ((fp = fopen(filename, "rb")) == 0) {
		printf("error opening\n");
		return(1);
	}

	read_u16(z80->main.af.w, fp);
	read_u16(z80->main.bc.w, fp);
	read_u16(z80->main.de.w, fp);
	read_u16(z80->main.hl.w, fp);
	read_u16(z80->alt.af.w, fp);
	read_u16(z80->alt.bc.w, fp);
	read_u16(z80->alt.de.w, fp);
	read_u16(z80->alt.hl.w, fp);
	read_u16(z80->pc, fp);
	read_u16(z80->sp, fp);
	read_u16(z80->ix.w, fp);
	read_u16(z80->iy.w, fp);
	read_u32(z80->cycles, fp);
	fread(mem, 0x10000, 1, fp);

	fclose(fp);
	return(0);
}

int main(int argc, char *argv[])
{
	char str[512];
	char *filename = 0;
	FILE *fp;
	long len;
	int i;
	Z80_STATE       state;
	long total = 0;
	int c,statecycles = 0;

//	test2();

	if (argc < 2) {
		printf("usage: %s test.rom\n",argv[0]);
		return(1);
	}

	filename = argv[1];
	printf("loading file %s\n", filename);

	//try to open the file
	if ((fp = fopen(filename, "rb")) == NULL) {
		printf("cannot open file.\n");
		return(1);
	}

	//find size of the file
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	//read file into memory
	fread((u8*)mem + 0x100, 1, len, fp);

	//close file
	fclose(fp);

	mem[0] = 0xd3;       /* OUT N, A */
	mem[1] = 0x00;

	mem[5] = 0xdb;       /* IN A, N */
	mem[6] = 0x00;
	mem[7] = 0xc9;       /* RET */

	memcpy(memory, mem, 0x10000);

	deadz80_init();
	z80 = deadz80_getcontext();

	for (i = 0; i < 16; i++) {
		z80->readpages[i] = (u8*)mem + (0x1000 * i);
		z80->writepages[i] = (u8*)mem + (0x1000 * i);
	}
	z80->ioreadfunc = ioread;
	z80->iowritefunc = iowrite;

	deadz80_reset();
	z80->pc = 0x100;

	Z80Reset(&state);
	state.pc = 0x100;

	state.registers.word[4] = z80->ix.w;
	state.registers.word[5] = z80->iy.w;

/*	state_load("state.bin");
	printf("loaded state\n");
	state.registers.word[3] = z80->main.af.w;
	state.registers.word[0] = z80->main.bc.w;
	state.registers.word[1] = z80->main.de.w;
	state.registers.word[2] = z80->main.hl.w;
	state.registers.word[4] = z80->ix.w;
	state.registers.word[5] = z80->iy.w;
	state.registers.word[6] = z80->sp;
	state.pc = z80->pc;
	memcpy(memory, mem, 0x10000);*/

	for (;;) {
		int error = 0;
		u16 prevpc = z80->pc;

//		deadz80_disassemble(str, z80->pc);
//		printf("%s\n", str);
		deadz80_step();
		if (z80->halt) {
			printf("halt\n");
			break;
		}
		
//		deadz80_disassemble(str, state.pc);
//		printf("$%04X :: %s\n", state.pc, str);
		c = Z80Emulate(&state, 1);
		total += c;
		statecycles += c;
		if (state.status & FLAG_STOP_EMULATION)
			break;

		if (total != z80->cycles)	{ printf("cycles doesnt match %ld should be %ld\n", z80->cycles, total); error = 1; };
		if (state.pc != z80->pc)	{ printf("pc doesnt match $%04X should be $%04X\n", z80->pc, state.pc); error = 1; };
		if (state.registers.word[6] != z80->sp)	{ printf("sp doesnt match $%04X should be $%04X\n", z80->sp, state.registers.word[6]); error = 1; };
		if (state.registers.word[3] != z80->regs->af.w)	{ printf("af doesnt match $%04X should be $%04X\n", z80->regs->af.w, state.registers.word[3]); error = 1; };
		if (state.registers.word[0] != z80->regs->bc.w)	{ printf("bc doesnt match $%04X should be $%04X\n", z80->regs->bc.w, state.registers.word[0]); error = 1; };
		if (state.registers.word[1] != z80->regs->de.w)	{ printf("de doesnt match $%04X should be $%04X\n", z80->regs->de.w, state.registers.word[1]); error = 1; };
		if (state.registers.word[2] != z80->regs->hl.w)	{ printf("hl doesnt match $%04X should be $%04X\n", z80->regs->hl.w, state.registers.word[2]); error = 1; };
		if (state.registers.word[4] != z80->ix.w)	{ printf("ix doesnt match $%04X should be $%04X\n", z80->ix.w, state.registers.word[4]); error = 1; };
		if (state.registers.word[5] != z80->iy.w)	{ printf("iy doesnt match $%04X should be $%04X\n", z80->iy.w, state.registers.word[5]); error = 1; };
		if (memcmp(mem, memory, 0x10000) != 0) {
			int n;

			printf("memory different!\n");
			for (n = 0; n < 0x10000; n++) {
				if (mem[n] != memory[n]) {
					printf("address $%04X:  $%02X != $%02X\n", n, mem[n], memory[n]);
				}
			}
			error = 1;
		}
		if (error) {
			deadz80_disassemble(str, prevpc);
			printf("%s\n", str);
			break;
		}
		total = 0;
		z80->cycles = 0;

/*		if (statecycles >= 0x00100000) {
			statecycles = 0;
			state_save("state.bin");
			printf("state saved\n");
		}*/
	}

	system("pause");

	return(0);
}
