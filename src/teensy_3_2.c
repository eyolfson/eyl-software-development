#include "teensy_3_2.h"

#include <stdbool.h>
#include <stdio.h>

struct registers {
	uint32_t r[16];
	uint32_t epsr;
};

static uint8_t *flash;

static bool is_branch;

static const char *get_address_name(uint32_t address)
{
	const char *name = NULL;

	switch (address) {
	case 0x4003D010:
		name = "RTC_CR";
		break;
	case 0x40048030:
		name = "SIM_SCGC3";
		break;
	case 0x40048038:
		name = "SIM_SCGC5";
		break;
	case 0x4004803C:
		name = "SIM_SCGC6";
		break;
	case 0x40048040:
		name = "SIM_SCGC7";
		break;
	case 0x40052000:
		name = "WDOG_STCTRLH";
		break;
	case 0x4005200E:
		name = "WDOG_UNLOCK";
		break;
	}

	return name;
}

static void memory_write_halfword(uint32_t address, uint16_t halfword)
{
	const char *name = get_address_name(address);

	printf("  > MEM[%08X]", address);
	if (name) {
		printf(" (%s)", name);
	}
	printf(" = %04X", halfword);

	if (address < 0x20008000) {
		flash[address] = (halfword & 0x00FF);
		flash[address + 1] = ((halfword & 0xFF00) >> 8);
	}
	else if (address == 0x40052000) {
		if (halfword == 0x0010) {
			printf(" (ALLOWUPDATE)");
		}
	}
	else if (address == 0x4005200E) {
		if (halfword == 0xC520) {
			printf(" (FIRST CODE)");
		}
		else if (halfword == 0xD928) {
			printf(" (SECOND CODE)");
		}
		else {
			printf(" (RESET)");
		}
	}

	printf("\n");
}

static void memory_write_word(uint32_t address, uint32_t word)
{
	const char *name = get_address_name(address);

	printf("  > MEM[%08X]", address);
	if (name) {
		printf(" (%s)", name);
	}
	printf(" = %08X", word);

	printf("\n");
}

static uint32_t word_at_address(uint32_t base)
{
	return flash[base] +
	       + (flash[base + 1] * 0x100)
	       + (flash[base + 2] * 0x10000)
	       + (flash[base + 3] * 0x1000000);
}

static uint32_t memory_read_word(uint32_t address)
{
	if (address < 0x20008000) {
		return word_at_address(address);
	}
	else {
		const char *name = get_address_name(address);
		if (name) {
			printf("  > %s defaulted to 0\n", name);
		}
		return 0;
	}
}

static uint16_t halfword_at_address(uint32_t base)
{
	return flash[base] +
	       + (flash[base + 1] * 0x100);
}

static void set_bit(uint32_t *v, uint8_t i) { *v |= (1 << i); }

static void a6_7_18_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t S = (first_halfword & 0x0400) >> 10;
	uint16_t imm10 = (first_halfword & 0x03FF);
	uint8_t J1 = (second_halfword & 0x2000) >> 13;
	uint8_t J2 = (second_halfword & 0x0800) >> 11;
	uint16_t imm11 = (second_halfword & 0x07FF);

	uint8_t I1 = (~(J1 ^ S)) & 1;
	uint8_t I2 = (~(J2 ^ S)) & 1;
	uint32_t imm32 = (I1 * 0x800000)
	                 + (I2 * 0x400000)
	                 + (imm10 * 0x1000)
	                 + (imm11 * 0x2);
	if (S == 1) {
		imm32 |= 0xFF000000;
	}

	/* The PC is a halfword ahead of where it should be, so instead of
	   adding 4 (for a normal read), we add 2 */
	uint32_t next_instr_addr = registers->r[15] + 4;
	uint32_t lr_value = next_instr_addr  | 0x1;
	uint32_t address = registers->r[15] + 4 + imm32;
	/* Set the last bit to zero */
	address &= 0xFFFFFFFE;

	printf("  BL label_%08X\n", address);
	registers->r[14] = lr_value;
	printf("  > R14 = %08X\n", lr_value);
	registers->r[15] = address;
	printf("  > R15 = %08X\n", address);

	is_branch = true;
}

static void a6_7_128_t1(struct registers *registers,
                       uint16_t first_halfword)
{
	uint8_t rt = (first_halfword & 0x0007);
	uint8_t rn = (first_halfword & 0x0038) >> 3;
	uint8_t imm5 = (first_halfword & 0x07C0) >> 6;
	uint32_t imm32 = imm5 << 1;

	uint32_t offset_addr = registers->r[rn] + imm32;
	uint32_t address = offset_addr;

	uint16_t value = registers->r[rt] & 0x0000FFFF;

	printf("  STRH R%d [R%d, #%d]\n", rt, rn, imm32);

	memory_write_halfword(address, value);
}

static void a6_7_20_t1(struct registers *registers,
                       uint16_t first_halfword)
{
	uint8_t rm = (first_halfword & 0x0078) >> 3;
	printf("  BX R%d\n", rm);
	uint32_t address = registers->r[rm] & ~(0x00000001);
	registers->r[15] = address;
	printf("  > R15 = %08X\n", address);

	is_branch = true;
}

static void a6_7_75_t1(struct registers *registers,
                       uint16_t first_halfword)
{
	uint8_t rd = (first_halfword & 0x0700) >> 8;
	uint8_t imm8 = (first_halfword & 0x00FF);
	printf("  MOVS R%d #%d\n", rd, imm8);
	registers->r[rd] = 0x00000000 + imm8;
	printf("  > R%d = %08X\n", rd, imm8);
	/* TODO: Set flags */
}

static void a6_7_75_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t S = (first_halfword & 0x0010) >> 4;
	/* TODO: Set flags */

	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t rd = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF);

	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;

	if ((imm12 & 0xC00) == 0x000) {
	}
	else {
		uint8_t rotate = (imm12 & 0xF80) >> 7;
		uint32_t unrotated_value = 0x80 + (imm12 & 0x7F);
		uint32_t result = unrotated_value << (32 - rotate);
		registers->r[rd] = result;
		printf("  MOV.W R%d #0x%08X\n", rd, result);
		printf("  > R%d = %08X\n", rd, result);
	}
}

static void a6_7_75_t3(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t imm4 = (first_halfword & 0x000F);
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t rd = (0x0F00 & second_halfword) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF);
	uint32_t imm32 = (imm4 * 0x1000)
	                 + (i * 0x0800)
	                 + (imm3 * 0x0100)
	                 + imm8;

	registers->r[rd] = imm32;
	printf("  MOVW R%d #0x%04X\n", rd, imm32);
	printf("  > R%d = %08X\n", rd, imm32);
}

static void a6_7_42_t1(struct registers *registers,
                        uint16_t first_halfword)
{
	uint8_t imm5 = (first_halfword & 0x07C0) >> 6;
	uint8_t rn = (first_halfword & 0x0038) >> 3;
	uint8_t rt = (first_halfword & 0x0007);
	uint32_t imm32 = imm5 << 2;

	uint32_t offset_addr = registers->r[rn] + imm32;
	uint32_t address = offset_addr;
	printf("  LDR R%d [R%d, #%d]\n", rt, rn, imm32);

	uint32_t data = memory_read_word(address);
	printf("  > R%d = %08X\n", rt, data);
	registers->r[rt] = data;
}

static void a6_7_119_t1(struct registers *registers,
                        uint16_t first_halfword)
{
	uint8_t imm5 = (first_halfword & 0x07C0) >> 6;
	uint8_t rn = (first_halfword & 0x0038) >> 3;
	uint8_t rt = (first_halfword & 0x0007);
	uint32_t imm32 = imm5 << 2;

	uint32_t offset_addr = registers->r[rn] + imm32;
	uint32_t address = offset_addr;
	printf("  STR R%d [R%d, #%d]\n", rt, rn, imm32);
	memory_write_word(address, registers->r[rt]);
}

static void a5_2_1(struct registers *registers,
                   uint16_t first_halfword)
{
	uint8_t opcode = (first_halfword & 0x3E00) >> 9;

	if ((opcode & 0x1C) == 0x10) {
		a6_7_75_t1(registers, first_halfword);
	}
}

static void a5_2_2(struct registers *registers,
                   uint16_t first_halfword)
{
}

static void a5_2_3(struct registers *registers,
                   uint16_t first_halfword)
{
	uint8_t opcode = (first_halfword & 0x03C0) >> 6;
	if ((opcode & 0xE) == 0xC) {
		a6_7_20_t1(registers, first_halfword);
	}
}

static void a5_2_4(struct registers *registers,
                   uint16_t first_halfword)
{
	uint8_t opA = (first_halfword & 0xF000) >> 12;
	uint8_t opB = (first_halfword & 0x0E00) >> 9;

	if (opA == 0x6) {
		if ((opB & 0x4) == 0x0) {
			a6_7_119_t1(registers, first_halfword);
		}
		else {
			a6_7_42_t1(registers, first_halfword);
		}
	}
	if (opA == 0x8) {
		if ((opB & 0x4) == 0x0) {
			a6_7_128_t1(registers, first_halfword);
		}
	}
}

static void a5_3_1(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op = (first_halfword & 0x01F0) >> 4;
	uint8_t rn = (first_halfword & 0x000F);
	if ((op & 0x1E)  == 0x04) {
		if (!(rn == 0xF)) {
		}
		else {
			a6_7_75_t2(registers, first_halfword, second_halfword);
		}
	}
}

static void a5_3_4(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op1 = (first_halfword & 0x07F0) >> 4;
	uint8_t op2 = (second_halfword & 0x7000) >> 12;

	if ((op2 & 0x05) == 0x05) {
		a6_7_18_t1(registers, first_halfword, second_halfword);
	}
}

static void a6_7_98_t1(struct registers *registers,
                       uint16_t first_word)
{
	uint8_t register_list = first_word & 0x00FF;
	uint8_t M = (first_word & 0x0100) >> 8;
	uint16_t all_registers = (M << 14) + register_list;

	uint8_t bit_count = __builtin_popcount(all_registers);
	uint32_t address = registers->r[13] - 4 * bit_count;
	printf("  PUSH {");
	bool first = false;
	for (uint8_t i = 0; i < 15; ++i) {
		if ((all_registers & (0x0001 << i)) == (0x0001 << i)) {
			if (!first) {
				first = true;
			}
			else {
				printf(", ");
			}
			printf("R%d", i);
		}
	}
	printf("}\n");

	for (uint8_t i = 0; i < 15; ++i) {
		if ((all_registers & (0x0001 << i)) == (0x0001 << i)) {
			flash[address & 0xFFFFFFFC] = registers->r[i];
			printf("  > MEM[%08X] = %08X (R%d)\n",
			       address & 0xFFFFFFFC, registers->r[i], i);
			address += 4;
		}
	}
	address = registers->r[13] - 4 * bit_count;
	registers->r[13] = address;
	printf("  > R13 = %08X\n", address);
}

static void a6_7_43_t1(struct registers *registers,
                       uint16_t first_halfword)
{
	/* LDR (literal) */
	uint8_t rt = (first_halfword & 0x0700) >> 8;
	uint8_t imm8 = first_halfword;
	/* TODO: What about Align(PC, 4)? */
	uint32_t address = (imm8 << 2) + registers->r[15] + 4;
	registers->r[rt] = word_at_address(address);
	printf("  LDR R%d [PC, #%d]\n", rt, imm8 << 2);
	printf("  > R%d = %08X\n", rt, registers->r[rt]);
}

/* 16-bit instruction encoding */
static void a5_2(struct registers *registers, uint16_t halfword)
{
	printf("%08X: %04X\n", registers->r[15], halfword);

	uint8_t opcode = (halfword & 0xFC00) >> 10;

	if ((opcode & 0x30) == 0x00) {
		a5_2_1(registers, halfword);
	}
	else if (opcode == 0x10) {
		a5_2_2(registers, halfword);
	}
	else if (opcode == 0x11) {
		a5_2_3(registers, halfword);
	}
	else if ((opcode & 0x3E) == 0x12) {
		a6_7_43_t1(registers, halfword);
	}
	else if ((opcode & 0x3C) == 0x14) {
		a5_2_4(registers, halfword);
	}
	else if ((opcode & 0x38) == 0x18) {
		a5_2_4(registers, halfword);
	}
	else if ((opcode & 0x38) == 0x20) {
		a5_2_4(registers, halfword);
	}
	else if ((halfword & 0xF000) == 0xB000) {
		/* A5.2.5 */
		uint8_t opcode = (halfword & 0x0FE0) >> 5;
		if ((opcode & 0x70) == 0x20) {
			a6_7_98_t1(registers, halfword);
		}
		else if ((opcode & 0x70) == 0x60) {
			/* POP */
		}
		else if ((opcode & 0x78) == 0x78) {
			/* If-Then and hints */
			uint8_t opA = (halfword & 0x00F0) >> 4;
			uint8_t opB = (halfword & 0x000F);
			if (opA == 0 && opB == 0) {
				/* A6.7.87 T1 */
				printf("  NOP\n");
			}
		}
	}
}

/* 32-bit instruction encoding */
static void a5_3(struct registers *registers,
                 uint16_t first_halfword,
                 uint16_t second_halfword)
{
	printf("%08X: %04X %04X\n", registers->r[15], first_halfword, second_halfword);

	uint8_t op1 = (first_halfword & 0x1800) >> 11;
	uint8_t op2 = (first_halfword & 0x07F0) >> 4;
	uint8_t op = (second_halfword & 0x8000) >> 15;

	if ((op1 == 0b10)
	     && ((op2 & 0b0100000) == 0b0000000)
	     && (op == 0)) {
		a5_3_1(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b10)
	     && ((op2 & 0b0100000) == 0b0100000)
	     && (op == 0)) {
		/* A5.3.3 */
		op = ((first_halfword & 0x01F0) >> 4);
		if (op == 0x04) {
			a6_7_75_t3(registers, first_halfword, second_halfword);
		}
	}
	else if ((op1 == 0b10)
	         && (op == 1)) {
		a5_3_4(registers, first_halfword, second_halfword);
	}
}

static void step(struct registers *registers)
{
	is_branch = false;

	uint16_t halfword = halfword_at_address(registers->r[15]);
	if (((halfword & 0xE000) == 0xE000)
	    && ((halfword & 0x1800) != 0x0000)) {
		uint16_t first_halfword = halfword;
		uint16_t second_halfword
			= halfword_at_address(registers->r[15] + 2);
		a5_3(registers, first_halfword, second_halfword);
		if (!is_branch) {
			registers->r[15] += 4;
		}
	}
	else {
		a5_2(registers, halfword);
		if (!is_branch) {
			registers->r[15] += 2;
		}
	}
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

	printf("\nExecution:\n");
	for (int i = 0; i < 23; ++i){
		step(&registers);
	}
}
