#include <stdio.h>
#include <string.h>
#include "deadz80.h"

/*
char *deadz80_GenerateRegNames(char *dest)
{
	sprintf(dest, "Main\n  AF:\n  BC:\n  DE:\n  HL:\nAlt\n  AF:\n  BC:\n  DE:\n  HL:\nPC:\nSP:\nIX:\nIY:\nIP:\nMR:\nIFF1:\nIFF2:");
	return(dest);
}

char *deadz80::GenerateRegData(char *dest)
{
	sprintf(dest, "\n$%04X\n$%04X\n$%04X\n$%04X\n\n$%04X\n$%04X\n$%04X\n$%04X\n$%04X\n$%04X\n$%04X\n$%04X\n$%02X\n$%02X\n$%02X\n$%02X",
		main.af.word, main.bc.word, main.de.word, main.hl.word,
		alt.af.word, alt.bc.word, alt.de.word, alt.hl.word,
		pc, sp, ix, iy, ip, mr, iff1, iff2);
	return(dest);
}
*/