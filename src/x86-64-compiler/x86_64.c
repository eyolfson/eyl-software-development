#include "x86_64.h"

static const uint8_t PREFIX_GROUP_1[] = { 0xF0, 0xF2, 0xF3 };
static const uint8_t PREFIX_GROUP_2[] = { 0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65 };
static const uint8_t PREFIX_GROUP_3[] = { 0x66 };
static const uint8_t PREFIX_GROUP_4[] = { 0x67 };

/* The opcode is 1, 2, or 3 bytes */
/* ?? */
/* 0x0F ?? */
/* 0x0F 0x38 ?? */
/* 0x0F 0x3A ?? */

int x86_64_decode(uint8_t *data, size_t size)
{
	/* Prefix [Opcode, may require prefixes] Mod.Reg.R/M or SIB byte
            [Displacment] [Immediate] */
	return 0;
}
