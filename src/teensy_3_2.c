#include "teensy_3_2.h"

#include <stdio.h>

struct registers {
	uint32_t r[16];
	uint32_t epsr;
};

static uint8_t *flash;

static uint32_t word_at_address(uint32_t base) {
	return flash[base] +
	       + (flash[base + 1] * 0x100)
	       + (flash[base + 2] * 0x10000)
	       + (flash[base + 3] * 0x1000000);
}

static uint16_t halfword_at_address(uint32_t base) {
	return flash[base] +
	       + (flash[base + 1] * 0x100);
}

static void set_bit(uint32_t *v, uint8_t i) { *v |= (1 << i); }

static void step(struct registers *registers) {
	uint16_t encoded = halfword_at_address(registers->r[15]);
	printf("%08X: %04X\n", registers->r[15], encoded);

	if ((encoded & 0xF800) == 0x4800) {
		/* LDR (literal) */
		uint8_t rt = (encoded & 0x0700) >> 8;
		uint8_t imm8 = encoded;
		uint32_t address = (imm8 << 2) + registers->r[15] + 4;
		registers->r[rt] = word_at_address(address);
		printf("  LDR R%d #%d\n", rt, imm8 << 2);
		printf("  > R%d = %08X\n", rt, registers->r[rt]);
	}
	if ((encoded & 0xE000) == 0xE000) {
		/* 32-bit instruction encoding */
		registers->r[15] += 2;
		uint16_t second_encoded = halfword_at_address(registers->r[15]);
		printf("          %04X\n", second_encoded);
	}

	registers->r[15] += 2;
}

/* SRAM_L = [0x1FFF8000, 0x20000000)
 * SRAM_U = [0x20000000, 0x20007FFF)
 */
void teensy_3_2_emulate(uint8_t *data, uint32_t length) {
	flash = data;

	struct registers registers;

	uint32_t initial_sp = word_at_address(0x00000000);
	uint32_t initial_pc = word_at_address(0x00000004);
	uint32_t nmi_address = word_at_address(0x00000008);

	printf("Initial Stack Pointer:   %08X\n", initial_sp);
	printf("Initial Program Counter: %08X\n", initial_pc);
	printf("NMI Address:             %08X\n", nmi_address);

	/* R15 (Program Counter):
     EPSR (Execution Program Status Register): bit 24 is the Thumb bit */
	const uint8_t EPSR_T_BIT = 24;

	registers.r[13] = initial_sp;
	registers.r[15] = initial_pc & 0xFFFFFFFE;
	registers.epsr = 0x01000000;
	if ((initial_pc & 0x00000001) == 0x00000001) {
		set_bit(&registers.epsr, EPSR_T_BIT);
	}

	//uint32_t r3 = word_at_address(0x02FC);

	printf("\nExecution:\n");
	for (int i = 0; i < 2; ++i){
		step(&registers);
	}
}
