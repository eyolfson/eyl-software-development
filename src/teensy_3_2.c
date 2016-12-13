#include "teensy_3_2.h"

#include <stdio.h>

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

/* SRAM_L = [0x1FFF8000, 0x20000000)
 * SRAM_U = [0x20000000, 0x20007FFF)
 */
void teensy_3_2_emulate(uint8_t *data, uint32_t length) {
	flash = data;

	uint32_t initial_sp = word_at_address(0x00000000);
	uint32_t initial_pc = word_at_address(0x00000004);
	uint32_t nmi_address = word_at_address(0x00000008);

	printf("Initial Stack Pointer:   %08X\n", initial_sp);
	printf("Initial Program Counter: %08X\n", initial_pc);
	printf("NMI Address:             %08X\n", nmi_address);

	/* R15 (Program Counter):
     EPSR (Execution Program Status Register): bit 24 is the Thumb bit */
	const uint8_t EPSR_T_BIT = 24;

	uint32_t r13 = initial_sp;
	uint32_t r15 = initial_pc & 0xFFFFFFFE;
	uint32_t epsr = 0x01000000;
	if ((initial_pc & 0x00000001) == 0x00000001) {
		set_bit(&epsr, EPSR_T_BIT);
	}

	uint32_t r3 = word_at_address(0x02FC);

	printf("\nExecution:\n");
	printf("%08X: %04X\n", r15, halfword_at_address(r15));
	r15 += 2;
	printf("%08X: %04X\n", r15, halfword_at_address(r15));
	r15 += 2;
	printf("%08X: %04X\n", r15, halfword_at_address(r15));
}

