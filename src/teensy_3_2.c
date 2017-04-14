#include "teensy_3_2.h"

#include "get_address_name.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/* SRAM_L = [0x1FFF8000, 0x20000000)
 * SRAM_U = [0x20000000, 0x20007FFF)
 */

#define SRAM_LOWER 0x1FFF8000
#define SRAM_UPPER 0x20007FFF

static uint8_t program_flash[0x40000]; // 256 KiB
static uint8_t eeprom[0x800];          //   2 KiB
static uint8_t sram[0x10000];          //  64 KiB

static uint8_t *flash;

static uint32_t MCG_S_reads = 0;
static uint32_t systick_millis_count_reads = 0;

static uint8_t memory_read(uint32_t address)
{
	if (address < 0x08000000) {
		return flash[address];
	}
	else if ((address >= SRAM_LOWER) && (address <= SRAM_UPPER)) {
		// systick_millis_count
		if (address == 0x1FFF8AE8) {
			++systick_millis_count_reads;
			if (systick_millis_count_reads == 1) {
				return 0;
			}
			else if (systick_millis_count_reads == 2) {
				return 4;
			}
			else if (systick_millis_count_reads == 3) {
				return 4;
			}
			else if (systick_millis_count_reads == 4) {
				return 4;
			}
			else if (systick_millis_count_reads == 5) {
				return 4;
			}
			return 5;
		}
		return sram[address - SRAM_LOWER];
	}
	else if ((address >= 0x40000000) && (address <= 0x4007FFFF)) {
		if (address == 0x40020000) {
			return 0x80;
		}
		else if (address == 0x40064006) {
			++MCG_S_reads;
			if (MCG_S_reads == 1) {
				return 0x02;
			}
			if (MCG_S_reads == 3) {
				return 0x08;
			}
			if (MCG_S_reads == 4) {
				return 0x20;
			}
			if (MCG_S_reads == 5) {
				return 0x40;
			}
			if (MCG_S_reads == 6) {
				return 0x0C;
			}
		}
		return 0;
	}
	else if ((address >= 0x42000000) && (address <= 0x43FFFFFF)) {
		// Intentionally left blank
		return 0;
	}
	else if ((address >= 0xE0000000) && (address <= 0xFFFFFFFF)) {
		return 0;
	}
	else {
		printf("memory_read(%08X)\n", address);
		assert(false);
	}
}

static uint8_t memory_byte_read(uint32_t address)
{
	uint8_t data = memory_read(address);
	printf("  > READ (%s) MemU[%08X,1] = %02X\n",
	       get_address_name(address), address, data);
	return data;
}

static uint16_t memory_halfword_read(uint32_t address)
{
	uint16_t data = memory_read(address)
	              | (memory_read(address + 1) << 8);
	// printf("  > READ MemU[%08X,2] = %04X\n", address, data);
	return data;
}

static uint32_t memory_word_read(uint32_t address)
{
	uint32_t data =  memory_read(address)
	                 | (memory_read(address + 1) << 8)
	                 | (memory_read(address + 2) << 16)
	                 | (memory_read(address + 3) << 24);
	printf("  > READ (%s) MemU[%08X,4] = %08X\n",
	       get_address_name(address), address, data);
	return data;
}

static void memory_write(uint32_t address, uint8_t data)
{
	if (address < 0x08000000) {
		assert(false);
		flash[address] = data;
	}
	else if ((address >= SRAM_LOWER) && (address <= SRAM_UPPER)) {
		sram[address - SRAM_LOWER] = data;
	}
	else if ((address >= 0x40000000) && (address <= 0x400FFFFF)) {
		// Intentionally left blank
	}
	else if ((address >= 0x42000000) && (address <= 0x43FFFFFF)) {
		// Intentionally left blank
	}
	else if ((address >= 0xE0000000) && (address <= 0xE00FFFFF)) {
		// Intentionally left blank
	}
	else {
		assert(false);
	}
}

static void memory_byte_write(uint32_t address, uint8_t data)
{
	const char *name = get_address_name(address);
	printf("  > (%s) MemU[%08X,1] = %02X\n", name, address, data);
	memory_write(address, data);
}

static void memory_halfword_write(uint32_t address, uint16_t data)
{
	const char *name = get_address_name(address);
	printf("  > (%s) MemU[%08X,2] = %04X\n", name, address, data);
	memory_write(address    , data      );
	memory_write(address + 1, data >>  8);
}

static void memory_word_write(uint32_t address, uint32_t data)
{
	const char *name = get_address_name(address);
	printf("  > (%s) MemU[%08X,4] = %08X\n", name, address, data);
	memory_write(address    , data      );
	memory_write(address + 1, data >>  8);
	memory_write(address + 2, data >> 16);
	memory_write(address + 3, data >> 24);
}

static uint32_t word_at_address(uint32_t base)
{
	return flash[base] +
	       + (flash[base + 1] * 0x100)
	       + (flash[base + 2] * 0x10000)
	       + (flash[base + 3] * 0x1000000);
}

struct registers {
	uint32_t r[16];
	uint32_t apsr;
	uint32_t ipsr;
	uint32_t epsr;
	uint32_t primask;
	uint32_t faultmask;
	uint8_t itstate;
};

struct AddWithCarry_Result {
	uint32_t result;
	bool carry_out;
	bool overflow;
};

enum SRType {
	SRType_None,
	SRType_LSL,
	SRType_LSR,
	SRType_ASR,
	SRType_ROR,
	SRType_RRX,
};

static bool is_branch;
static bool is_it_inst;

struct ShiftTNTuple {
	enum SRType shift_t;
	uint8_t shift_n;
};

struct ResultCarryTuple {
	uint32_t result;
	bool carry;
};

struct ResultCarryOverflowTuple {
	uint32_t result;
	bool carry;
	bool overflow;
};

static struct ResultCarryTuple LSL_C(uint32_t x, uint8_t shift)
{
	assert(shift > 0);
	uint64_t extended_x = x;
	extended_x <<= shift;
	struct ResultCarryTuple T;
	T.result = (extended_x & 0xFFFFFFFF);
	T.carry = (extended_x & 0x100000000);
	return T;
}

static uint32_t LSL(uint32_t x, uint8_t shift)
{
	if (shift == 0) {
		return x;
	}
	else {
		struct ResultCarryTuple T = LSL_C(x, shift);
		return T.result;
	}
}

static struct ResultCarryTuple LSR_C(uint32_t x, uint8_t shift)
{
	assert(shift > 0);
	uint64_t extended_x = x;
	struct ResultCarryTuple T;
	T.result = (extended_x >> shift) & 0xFFFFFFFF;
	T.carry = ((extended_x >> (shift - 1)) & 0x1) == 0x1;
	return T;
}

static uint32_t LSR(uint32_t x, uint8_t shift)
{
	if (shift == 0) {
		return x;
	}
	else {
		struct ResultCarryTuple T = LSR_C(x, shift);
		return T.result;
	}
}

static struct ResultCarryTuple ASR_C(uint32_t x, uint8_t shift)
{
	assert(shift > 0);
	uint64_t extended_x = x;
	if ((x & 0x80000000) == 0x80000000) {
		extended_x |= ((uint64_t) 0xFFFFFFFF00000000);
	}
	struct ResultCarryTuple T;
	T.result = (extended_x >> shift) & 0xFFFFFFFF;
	T.carry = (extended_x & (1 << (shift - 1)))
	          == (((uint64_t) 1) << (shift - 1));
	return T;
}

static struct ResultCarryTuple ROR_C(uint32_t x, uint8_t shift)
{
	assert(shift != 0);
	uint8_t m = shift % 32;
	struct ResultCarryTuple T;
	T.result = LSR(x, m) | LSL(x, 32 - m);
	T.carry = (T.result & 0x80000000) == 0x80000000;
	return T;
}

static struct ResultCarryTuple Shift_C(uint32_t value, enum SRType type,
                                       uint8_t amount, bool carry_in)
{
	struct ResultCarryTuple T;
	if (amount == 0) {
		T.result = value;
		T.carry = carry_in;
	}
	else {
		switch (type) {
		case SRType_LSL:
			T = LSL_C(value, amount);
			break;
		case SRType_LSR:
			T = LSR_C(value, amount);
			break;
		case SRType_ASR:
			T = ASR_C(value, amount);
			break;
		case SRType_ROR:
			T = ROR_C(value, amount);
			break;
		default:
			assert(false);
		}
	}

	return T;
}

static uint32_t Shift(uint32_t value, enum SRType type,
                      uint8_t amount, bool carry_in)
{
	struct ResultCarryTuple T = Shift_C(value, type, amount, carry_in);
	return T.result;
}

uint8_t APSR_N(struct registers *registers)
{
	return (registers->apsr & 0x80000000) >> 31;
}

uint8_t APSR_Z(struct registers *registers)
{
	return (registers->apsr & 0x40000000) >> 30;
}

uint8_t APSR_C(struct registers *registers)
{
	return (registers->apsr & 0x20000000) >> 29;
}

uint8_t APSR_V(struct registers *registers)
{
	return (registers->apsr & 0x10000000) >> 28;
}

uint8_t APSR_Q(struct registers *registers)
{
	return (registers->apsr & 0x08000000) >> 27;
}

void APSR_N_set(struct registers *registers)
{
	registers->apsr |= 0x80000000;
}

void APSR_Z_set(struct registers *registers)
{
	registers->apsr |= 0x40000000;
}

void APSR_C_set(struct registers *registers)
{
	registers->apsr |= 0x20000000;
}

void APSR_V_set(struct registers *registers)
{
	registers->apsr |= 0x10000000;
}

void APSR_N_clear(struct registers *registers)
{
	registers->apsr &= ~0x80000000;
}

void APSR_Z_clear(struct registers *registers)
{
	registers->apsr &= ~0x40000000;
}

void APSR_C_clear(struct registers *registers)
{
	registers->apsr &= ~0x20000000;
}

void APSR_V_clear(struct registers *registers)
{
	registers->apsr &= ~0x10000000;
}

void setflags_ResultCarryTuple(struct registers *registers,
                               struct ResultCarryTuple T)
{
	if ((T.result & 0x80000000) == 0x80000000) { APSR_N_set(registers); }
	else                                       { APSR_N_clear(registers); }

	if (T.result == 0) { APSR_Z_set(registers); }
	else               { APSR_Z_clear(registers); }

	if (T.carry) { APSR_C_set(registers); }
	else         { APSR_C_clear(registers); }
}

void setflags_ResultCarryOverflowTuple(struct registers *registers,
                                       struct ResultCarryOverflowTuple T)
{
	if ((T.result & 0x80000000) == 0x80000000) { APSR_N_set(registers); }
	else                                       { APSR_N_clear(registers); }

	if (T.result == 0) { APSR_Z_set(registers); }
	else               { APSR_Z_clear(registers); }

	if (T.carry) { APSR_C_set(registers); }
	else         { APSR_C_clear(registers); }

	if (T.overflow) { APSR_V_set(registers); }
	else            { APSR_V_clear(registers); }
}

struct ResultCarryOverflowTuple AddWithCarry(uint32_t x, uint32_t y, bool carry_in)
{
	struct ResultCarryOverflowTuple T;


	uint64_t unsigned_sum = ((uint64_t) x) + ((uint64_t) y);
	if (carry_in) unsigned_sum += 1;

	int64_t signed_sum = ((int32_t) x) + ((int32_t) y);
	if (carry_in) signed_sum += 1;

	T.result = (unsigned_sum & 0xFFFFFFFF);

	if (((uint64_t) T.result) == unsigned_sum)
		T.carry = false;
	else
		T.carry = true;

	if (((int64_t) ((int32_t) T.result)) == signed_sum)
		T.overflow = false;
	else
		T.overflow = true;

	return T;
}

void ITAdvance(struct registers *registers)
{
	if ((registers->itstate & 0b00000111) == 0b00000000) {
		registers->itstate = 0;
	}
	else {
		uint8_t old_state = registers->itstate & 0b00011111;
		uint8_t new_state = (old_state << 1) & 0b00011111;
		registers->itstate = (registers->itstate & 0b11100000)
		                     | new_state;
	}
	printf("  > ITSTATE = %02X\n", registers->itstate);
}

bool InITBlock(struct registers *registers)
{
	return (registers->itstate & 0b00001111) != 0b0000;
}

bool LastInITBlock(struct registers *registers)
{
	return (registers->itstate & 0b00001111) == 0b1000;
}

uint8_t CurrentCond(struct registers *registers)
{
	// Directly from branch instructions
	uint16_t first_halfword = memory_halfword_read(registers->r[15]);
	if ((first_halfword & 0xF000) == 0xD000) {
		uint8_t cond = (first_halfword & 0x0F00) >> 8;
		return cond;
	}
	else if ((first_halfword & 0xF800) == 0xF000) {
		uint16_t second_halfword = memory_halfword_read(registers->r[15] + 2);
		if ((second_halfword & 0xD000) == 0x8000) {
			uint8_t cond = (first_halfword & 0x03C0) >> 6;
			return cond;
		}
	}

	if (InITBlock(registers)) {
		return (registers->itstate & 0xF0) >> 4;
	}

	return 0b1111; // TODO
}

bool ConditionPassed(struct registers *registers)
{
	uint8_t cond = CurrentCond(registers);

	bool result;
	switch (cond & 0b1110) {
	case 0b0000:
		result = APSR_Z(registers) == 1;
		break;
	case 0b0010:
		result = APSR_C(registers) == 1;
		break;
	case 0b0100:
		result = APSR_N(registers) == 1;
		break;
	case 0b0110:
		result = APSR_V(registers) == 1;
		break;
	case 0b1000:
		result = (APSR_C(registers) == 1) && (APSR_Z(registers) == 0);
		break;
	case 0b1010:
		result = APSR_N(registers) == APSR_V(registers);
		break;
	case 0b1100:
		result = (APSR_N(registers) == APSR_V(registers))
		         && (APSR_Z(registers) == 0);
		break;
	case 0b1110:
		result = true;
		break;
	}

	if (((cond & 0b0001) == 0b0001) && (cond != 0b1111)) {
		result = !result;
	}

	return result;
}

uint32_t SP(struct registers *registers)
{
	return registers->r[13];
}

/* A4.2.2 */
uint32_t PC(struct registers *registers)
{
	return registers->r[15] + 4;
}

uint32_t Align_PC_4(struct registers *registers)
{
	return PC(registers) & 0xFFFFFFFC;
}

struct ShiftTNTuple DecodeImmShift(uint8_t type, uint8_t imm5)
{
	struct ShiftTNTuple T;

	switch (type) {
	case 0b00:
		T.shift_t = SRType_LSL;
		T.shift_n = imm5;
		break;
	case 0b01:
		T.shift_t = SRType_LSR;
		if (imm5 == 0b00000) {
			T.shift_n = 32;
		}
		else {
			T.shift_n = imm5;;
		}
		break;
	case 0b10:
		T.shift_t = SRType_ASR;
		if (imm5 == 0b00000) {
			T.shift_n = 32;
		}
		else {
			T.shift_n = imm5;;
		}
		break;
	case 0b11:
		if (imm5 == 0b00000) {
			T.shift_t = SRType_RRX;
			T.shift_n = 1;
		}
		else {
			T.shift_t = SRType_ROR;
			T.shift_n = imm5;
		}
		break;
	}

	return T;
}

struct ResultCarryTuple ThumbExpandImm_C(uint16_t imm12, bool carry) {
	struct ResultCarryTuple T;

	if ((imm12 & 0xC00) == 0x000) {
		uint8_t switch_value = (imm12 & 0x300) >> 8;
		switch (switch_value) {
		case 0b00:
			T.result = (imm12 & 0x0FF);
			break;
		case 0b01:
			assert(false);
			break;
		case 0b10:
			assert(false);
			break;
		case 0b11:
			assert(false);
			break;
		}
		T.carry = carry;
	}
	else {
		uint32_t unrotated_value = 0x80 + (imm12 & 0x7F);
		uint8_t shift = (imm12 & 0xF80) >> 7;
		T = ROR_C(unrotated_value, shift);
	}

	return T;
}

uint32_t ThumbExpandImm(struct registers *registers,
                        uint16_t imm12)
{
	return ThumbExpandImm_C(imm12, APSR_C(registers)).result;
}

static const char *get_condition_name(uint8_t cond)
{
	const char *field;
	switch (cond) {
	case 0b0000:
		field = "EQ";
		break;
	case 0b0001:
		field = "NE";
		break;
	case 0b0010:
		field = "CS";
		break;
	case 0b0011:
		field = "CC";
		break;
	case 0b0100:
		field = "MI";
		break;
	case 0b0101:
		field = "PL";
		break;
	case 0b0110:
		field = "VS";
		break;
	case 0b0111:
		field = "VC";
		break;
	case 0b1000:
		field = "HI";
		break;
	case 0b1001:
		field = "LS";
		break;
	case 0b1010:
		field = "GE";
		break;
	case 0b1011:
		field = "LT";
		break;
	case 0b1100:
		field = "GT";
		break;
	case 0b1101:
		field = "LE";
		break;
	case 0b1110:
		field = "";
		break;
	case 0b1111:
		field = "";
		break;
	default:
		assert(false);
	}
	return field;
}

static const char *get_condition_field(struct registers *registers)
{
	uint8_t cond = CurrentCond(registers);
	return get_condition_name(cond);
}

/*
uint8_t APSR_N(struct registers *registers)
{
	return (registers->apsr & 0x80000000) >> 31;
}

uint8_t APSR_Z(struct registers *registers)
{
	return (registers->apsr & 0x40000000) >> 30;
}

uint8_t APSR_C(struct registers *registers)
{
	return (registers->apsr & 0x20000000) >> 29;
}

uint8_t APSR_V(struct registers *registers)
{
	return (registers->apsr & 0x10000000) >> 28;
}
*/

static void set_bit(uint32_t *v, uint8_t i) { *v |= (1 << i); }

static void b4_1_1_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t im = (halfword & 0x0010) >> 4;
	uint8_t I = (halfword & 0x0002) >> 1;
	uint8_t F = (halfword & 0x0001) >> 0;

	bool enable = im == 0;
	bool disable = im == 1;
	bool affectPRI = I == 1;
	bool affectFAULT = F == 1;

	printf("  CPS");
	if (enable) {
		printf("IE");
		if (affectPRI) {
			printf(" i");
			registers->primask = 0;
		}
		if (affectFAULT) {
			if (!affectPRI) {
				printf(" ");
			}
			printf("f");
			registers->faultmask = 0;
		}
	}
	else if (disable) {
		printf("ID");
		if (affectPRI) {
			printf(" i");
			registers->primask = 1;
		}
		// TODO Priority
		if (affectFAULT) {
			if (!affectPRI) {
				printf(" ");
			}
			printf("f");
			registers->faultmask = 1;
		}
	}
	printf("\n");
}

static void ADD_immediate(struct registers *registers, uint8_t d, uint8_t n,
                          bool setflags, uint32_t imm32)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[n], imm32, false);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_3_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t imm3 = (halfword & 0x01C0) >> 6;
	uint8_t n    = (halfword & 0x0038) >> 3;
	uint8_t d    = (halfword & 0x0007) >> 0;

	bool setflags = !InITBlock(registers);
	uint32_t imm32 = imm3;

	printf("  ADD");
	if (setflags) {
		printf("S");
	}
	else {
		printf("%s", get_condition_field(registers));
	}
	printf(" R%d, R%d, #%d\n", d, n, imm32);

	ADD_immediate(registers, d, n, setflags, imm32);
}

static void a6_7_3_t2(struct registers *registers, uint16_t halfword)
{
	uint8_t d    = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint8_t n = d;
	bool setflags = !InITBlock(registers);

	uint32_t imm32 = imm8;

	struct ResultCarryOverflowTuple T =
		AddWithCarry(registers->r[n], imm32, false);

	printf("  ADD");
	if (setflags) {
		printf("S");
	}
	printf(" R%d #%d\n", d, imm32);

	ADD_immediate(registers, d, n, setflags, imm32);
}

static void a6_7_3_t3(struct registers *registers,
                      uint16_t first_halfword,
                      uint16_t second_halfword)
{
	uint8_t i    =  (first_halfword & 0x0400) >> 10;
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;

	bool setflags = S == 1;
	uint32_t imm32 = ThumbExpandImm(registers, imm12);

	printf("  ADD");
	if (setflags) {
		printf("S");
	}
	printf("%s.W R%d, R%d, #%d\n",
	       get_condition_field(registers), d, n, imm32);

	ADD_immediate(registers, d, n, setflags, imm32);
}

static void ADD_register(struct registers *registers, bool is_wide,
                         uint8_t d, uint8_t n, uint8_t m,
                         bool setflags, enum SRType shift_t, uint8_t shift_n)
{
	printf("  ADD");
	if (setflags) {
		printf("S");
	}
	printf("%s", get_condition_field(registers));
	if (is_wide) {
		printf(".W");
	}
	printf(" R%d, R%d, R%d", d, n, m);
	if (shift_n != 0) {
		printf(", <shift>");
	}
	printf("\n");

	if (ConditionPassed(registers)) {
		uint32_t shifted = Shift(registers->r[m], shift_t,
		                         shift_n, APSR_C(registers));
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[n], shifted, false);
		if (d == 15) {
			assert(false); // TODO
		}
		else {
			registers->r[d] = T.result;
			printf("  > R%d = %08X\n", d, registers->r[d]);
			if (setflags) {
				setflags_ResultCarryOverflowTuple(registers, T);
				printf("  > APSR = %08X\n", registers->apsr);
			}
		}
	}
}

static void a6_7_4_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;

	bool setflags = !InITBlock(registers);
	if (setflags) {
		printf("  ADDS R%d, R%d, R%d\n", d, n, m);
	}
	else {
		printf("  ADD%s R%d, R%d, R%d\n",
		       get_condition_field(registers), d, n, m);
	}

	if (ConditionPassed(registers)) {
		uint32_t shifted = registers->r[m];
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[n], shifted, false);

		assert(d != 15);

		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_4_t2(struct registers *registers, uint16_t halfword)
{
	uint8_t DN = (halfword & 0x0080) >> 7;
	uint8_t m = (halfword & 0x0078) >> 3;
	uint8_t Rdn = (halfword & 0x0007) >> 0;

	uint8_t dn = (DN << 3) | Rdn;

	bool setflags = false;

	printf("  ADD%s R%d, R%d\n", get_condition_field(registers), dn, m);

	uint32_t shifted = registers->r[m];
	struct ResultCarryOverflowTuple T = AddWithCarry(registers->r[dn],
	                                                 shifted, false);

	assert(dn != 15);

	registers->r[dn] = T.result;
	printf("  > R%d = %08X\n", dn, registers->r[dn]);
}

static void a6_7_4_t3(struct registers *registers,
                      uint16_t first_halfword,
                      uint16_t second_halfword)
{
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm2 = (second_halfword & 0x00C0) >> 6;
	uint8_t type = (second_halfword & 0x0030) >> 4;
	uint8_t m = (second_halfword & 0x000F) >> 0;

	bool setflags = S == 1;
	uint8_t imm5 = (imm3 << 2) | imm2;
	struct ShiftTNTuple T = DecodeImmShift(type, imm5);

	ADD_register(registers, true, d, n, m, setflags, T.shift_t, T.shift_n);
}

static void ADD_SP_plus_immediate(struct registers *registers,
                                  uint8_t d, uint32_t imm32, bool setflags)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryOverflowTuple T;
		T = AddWithCarry(SP(registers), imm32, false);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_5_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t d = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;
	bool setflags = false;

	uint32_t imm32 = imm8 << 2;

	printf("  ADD%s R%d, SP, #%d\n", get_condition_field(registers), d, imm8);

	ADD_SP_plus_immediate(registers, d, imm32, setflags);
}

static void a6_7_5_t2(struct registers *registers, uint16_t halfword)
{
	uint8_t d = 13;
	bool setflags = false;

	uint8_t imm7 = (halfword & 0x007F) >> 0;
	uint32_t imm32 = imm7 << 2;

	printf("  ADD%s SP, SP, #%d\n", get_condition_field(registers), imm7);

	ADD_SP_plus_immediate(registers, d, imm32, setflags);
}

static void a6_7_8_t1(struct registers *registers,
                      uint16_t first_halfword,
                      uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	if (d == 0xF && S == 1) {
		assert(false);
	}

	bool setflags = S == 1;
	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;
	struct ResultCarryTuple T = ThumbExpandImm_C(imm12, APSR_C(registers));

	if (setflags) {
		printf("  ANDS%s R%d, R%d, #0x%08X\n",
		       get_condition_field(registers), d, n, T.result);
	}
	else {
		printf("  AND%s R%d, R%d, #0x%08X\n",
		        get_condition_field(registers), d, n, T.result);
	}

	if (ConditionPassed(registers)) {
		int32_t result = registers->r[n] & T.result;
		registers->r[d] = result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			if ((result & 0x80000000) == 0x80000000) {
				APSR_N_set(registers);
			}
			else {
				APSR_N_clear(registers);
			}
			if (result == 0) {
				APSR_Z_set(registers);
			}
			else {
				APSR_Z_clear(registers);
			}
			if (T.carry) {
				APSR_C_set(registers);
			}
			else {
				APSR_C_clear(registers);
			}
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void ASR_immediate(struct registers *registers,
                          uint8_t d, uint8_t shift_n, uint8_t m, bool setflags)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryTuple T =
			Shift_C(registers->r[m], SRType_ASR, shift_n,
			        APSR_C(registers));
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_10_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t m = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;

	bool setflags = !InITBlock(registers);

	struct ShiftTNTuple T = DecodeImmShift(0b10, imm5);
	uint8_t shift_n = T.shift_n;

	printf("  ASR");
	if (setflags) {
		printf("S");
	}
	else {
		printf("%s", get_condition_field(registers));
	}
	printf(" R%d, R%d, #%d\n", d, m, imm5);

	ASR_immediate(registers, d, shift_n, m, setflags);
}

static void a6_7_10_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm2 = (second_halfword & 0x00C0) >>  6;
	uint8_t m    = (second_halfword & 0x000F) >>  0;

	bool setflags = S == 1;
	uint8_t imm5 = (imm3 << 2) | imm2;

	struct ShiftTNTuple T = DecodeImmShift(0b10, imm5);
	uint8_t shift_n = T.shift_n;

	assert(!(d == 13 || d == 15 || m == 13 || m == 15));

	printf("  ASR");
	if (setflags) {
		printf("S");
	}
	printf("%s.W R%d, R%d, #%d\n",
	       get_condition_field(registers), d, m, imm5);

	ASR_immediate(registers, d, shift_n, m, setflags);
}

static void BranchTo(struct registers *registers, uint32_t address)
{
	registers->r[15] = address;
	printf("  > PC = %08X\n", registers->r[15]);
	is_branch = true;
}

static void BranchWritePC(struct registers *registers, uint32_t address)
{
	BranchTo(registers, address & ~(0x1));
}

static void BXWritePC(struct registers *registers, uint32_t address)
{
	assert((address & 0x00000001) == 0x00000001);
	BranchTo(registers, address & 0xFFFFFFFE);
}

static void LoadWritePC(struct registers *registers, uint32_t address)
{
	BXWritePC(registers, address);
}

static void B(struct registers *registers, uint32_t imm32)
{
	if (ConditionPassed(registers)) {
		BranchWritePC(registers, PC(registers) + imm32);
	}
}

static void a6_7_12_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t cond = (halfword & 0x0F00) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;
	uint32_t imm32 = imm8 << 1;

	// SignExtend
	if ((imm32 & (1 << 8)) == (1 << 8)) {
		imm32 |= 0xFFFFFE00;
	}

	uint32_t address = PC(registers) + imm32;
	printf("  B%s label_%08X\n", get_condition_field(registers), address);

	B(registers, imm32);
}

static void a6_7_12_t2(struct registers *registers, uint16_t halfword)
{
	uint16_t imm11 = (halfword & 0x07FF) >> 0;
	uint32_t imm32 = imm11 << 1;


	if ((imm32 & (1 << 11)) == (1 << 11)) {
		imm32 |= 0xFFFFF000;
	}

	uint32_t address = PC(registers) + imm32;
	printf("  B%s label_%08X\n", get_condition_field(registers), address);

	B(registers, imm32);
}

static void a6_7_12_t3(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t  S     =  (first_halfword & 0x0400) >> 10;
	uint8_t  cond  =  (first_halfword & 0x03C0) >>  6;
	uint8_t  imm6  =  (first_halfword & 0x003F) >>  0;
	uint8_t  J1    = (second_halfword & 0x2000) >> 13;
	uint8_t  J2    = (second_halfword & 0x0800) >> 11;
	uint16_t imm11 = (second_halfword & 0x07FF) >>  0;

	uint32_t imm32 = (S << 20)
	                 | (J2 << 19)
	                 | (J1 << 18)
	                 | (imm6 << 12)
	                 | (imm11 << 1);
	if (S == 1) {
		imm32 |= 0xFFE00000;
	}

	assert(!InITBlock(registers));

	uint32_t address = PC(registers) + imm32;
	printf("  B%s label_%08X\n", get_condition_field(registers), address);

	B(registers, imm32);
}

static void a6_7_12_t4(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t  S     =  (first_halfword & 0x0400) >> 10;
	uint16_t imm10 =  (first_halfword & 0x03FF) >>  0;
	uint8_t  J1    = (second_halfword & 0x2000) >> 13;
	uint8_t  J2    = (second_halfword & 0x0800) >> 11;
	uint16_t imm11 = (second_halfword & 0x07FF) >>  0;

	uint8_t I1 = (~(J1 ^ S)) & 1;
	uint8_t I2 = (~(J2 ^ S)) & 1;
	uint32_t imm32 = (I1 * 0x800000)
	                 + (I2 * 0x400000)
	                 + (imm10 * 0x1000)
	                 + (imm11 * 0x2);
	if (S == 1) {
		imm32 |= 0xFF000000;
	}

	uint32_t address = PC(registers) + imm32;
	printf("  B%s label_%08X\n", get_condition_field(registers), address);

	B(registers, imm32);
}

static void BIC_immediate(struct registers *registers, uint8_t d, uint8_t n,
                          bool setflags, int32_t imm32, bool carry)
{
	if (ConditionPassed(registers)) {
		uint32_t result = registers->r[n] & (~imm32);
		registers->r[d] = result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			if ((result & 0x80000000) == 0x80000000) {
				APSR_N_set(registers);
			}
			else {
				APSR_N_clear(registers);
			}
			if (result == 0) {
				APSR_Z_set(registers);
			}
			else {
				APSR_Z_clear(registers);
			}
			if (carry) {
				APSR_C_set(registers);
			}
			else {
				APSR_C_clear(registers);
			}
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_15_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i    =  (first_halfword & 0x0400) >> 10;
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;
	bool setflags = S == 1;
	struct ResultCarryTuple T = ThumbExpandImm_C(imm12, APSR_C(registers));
	uint32_t imm32 = T.result;
	bool carry = T.carry;
	assert(!(d == 13 || d == 15 || n == 13 || n == 15));

	printf("  BIC");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, R%d, #0x%08X\n",
	       get_condition_field(registers), d, n, imm32);

	BIC_immediate(registers, d, n, setflags, imm32, carry);
}

static void a6_7_18_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t  S     =  (first_halfword & 0x0400) >> 10;
	uint16_t imm10 =  (first_halfword & 0x03FF) >>  0;
	uint8_t  J1    = (second_halfword & 0x2000) >> 13;
	uint8_t  J2    = (second_halfword & 0x0800) >> 11;
	uint16_t imm11 = (second_halfword & 0x07FF) >>  0;

	uint8_t I1 = (~(J1 ^ S)) & 1;
	uint8_t I2 = (~(J2 ^ S)) & 1;
	uint32_t imm32 = (I1 * 0x800000)
	                 + (I2 * 0x400000)
	                 + (imm10 * 0x1000)
	                 + (imm11 * 0x2);
	if (S == 1) {
		imm32 |= 0xFF000000;
	}

	uint32_t next_instr_addr = registers->r[15] + 4;
	uint32_t lr_value = next_instr_addr | 0x1;
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

static void a6_7_19_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t m = (halfword & 0x0078) >> 3;

	printf("  BLX%s R%d\n", get_condition_field(registers), m);

	if (ConditionPassed(registers)) {
		uint32_t target = registers->r[m];
		uint32_t next_instr_address = PC(registers) - 2;
		registers->r[14] = next_instr_address | 0b1;
		printf("  > R14 = %08X\n", registers->r[14]);
		BXWritePC(registers, target);

		is_branch = true;
	}
}

static void a6_7_20_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t m = (halfword & 0x0078) >> 3;
	printf("  BX R%d\n", m);
	uint32_t address = registers->r[m] & ~(0x00000001);
	registers->r[15] = address;
	printf("  > R15 = %08X\n", address);

	is_branch = true;
}

static void a6_7_21_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t op = (halfword & 0x0800) >> 11;
	uint8_t i = (halfword & 0x0200) >> 9;
	uint8_t imm5 = (halfword & 0x00F8) >> 3;
	uint8_t rn = (halfword & 0x0007);

	uint32_t imm32 = (i * 0x40)
	                 + (imm5 * 0x2);
	uint32_t address = PC(registers) + imm32;

	printf("  CB");
	if (op == 1){
		printf("N");
	}
	printf("Z R%d, %08X\n", rn, address);

	if ((op == 0 && registers->r[rn] == 0)
	    || (op == 1 && registers->r[rn] != 0)) {
		registers->r[15] = address;
		printf("  > R15 = %08X\n", address);
		is_branch = true;
	}
}

static void CMP(struct registers *registers, uint8_t n, uint32_t imm32)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[n], ~imm32, true);
		setflags_ResultCarryOverflowTuple(registers, T);
		printf("  > APSR = %08X\n", registers->apsr);
	}
}

static void a6_7_27_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t n = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;

	printf("  CMP%s R%d, #%d\n",
	       get_condition_field(registers),  n, imm32);
	CMP(registers, n, imm32);
}

static void a6_7_27_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i    =  (first_halfword & 0x0400) >> 10;
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;

	uint32_t imm32 = ThumbExpandImm(registers, imm12);

	assert(n != 15);

	printf("  CMP%s R%d, #%d\n", get_condition_field(registers), n, imm32);
	CMP(registers, n, imm32);
}

static void a6_7_28_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t m = (halfword & 0x0038) >> 3;
	uint8_t n = (halfword & 0x0007) >> 0;

	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	// TODO use proper CMP register function
	assert(shift_n == 0);
	uint32_t shifted = registers->r[m];

	printf("  CMP%s R%d, R%d\n",
	       get_condition_field(registers), n, m);
	CMP(registers, n, shifted);
}

static void a6_7_28_t2(struct registers *registers, uint16_t halfword)
{
	uint8_t N  = (halfword & 0x0080) >> 7;
	uint8_t m  = (halfword & 0x0078) >> 3;
	uint8_t Rn = (halfword & 0x0007) >> 0;

	uint8_t n = (N << 3) | Rn;

	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	assert(!((n < 8) && (m < 8)));
	assert(!((n == 15) || (m == 15)));

	// TODO use proper CMP register function
	assert(shift_n == 0);
	uint32_t shifted = registers->r[m];

	printf("  CMP%s R%d, R%d\n",
	       get_condition_field(registers), n, m);
	CMP(registers, n, shifted);
}

static void a6_7_37_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t firstcond = (halfword & 0x00F0) >> 4;
	uint8_t mask      = (halfword & 0x000F) >> 0;

	uint8_t firstcond0 = (firstcond & 0b0001) >> 0;

	uint8_t mask3 = (mask & 0b1000) >> 3;
	uint8_t mask2 = (mask & 0b0100) >> 2;
	uint8_t mask1 = (mask & 0b0010) >> 1;
	uint8_t mask0 = (mask & 0b0001) >> 0;

	printf("  IT", get_condition_name(firstcond));
	if (mask0 == 1) {
		if (mask3 == firstcond0) { printf("T"); }
		else                     { printf("E"); }
		if (mask2 == firstcond0) { printf("T"); }
		else                     { printf("E"); }
		if (mask1 == firstcond0) { printf("T"); }
		else                     { printf("E"); }
	}
	else if (mask1 == 1) {
		if (mask3 == firstcond0) { printf("T"); }
		else                     { printf("E"); }
		if (mask2 == firstcond0) { printf("T"); }
		else                     { printf("E"); }
	}
	else if (mask2 == 1) {
		if (mask3 == firstcond0) { printf("T"); }
		else                     { printf("E"); }
	}
	else {
		assert(mask == 0b1000);
	}

	printf(" %s\n", get_condition_name(firstcond));

	registers->itstate = (halfword & 0x00FF) >> 0;
	printf("  > ITSTATE = %02X\n", registers->itstate);

	is_it_inst = true;
}

static void LDR_immediate(struct registers *registers,
                          uint8_t t, uint8_t n, uint32_t imm32,
                          bool index, bool add, bool wback)
{
	if (index) {
		if (!wback) {
			printf("  LDR%s R%d, [R%d, #",
			       get_condition_field(registers), t, n);
			if (add) {
				printf("+");
			}
			else {
				printf("-");
			}
			printf("%d]\n", imm32);
		}
		else {
			printf("  LDR%s R%d, [R%d, #",
			       get_condition_field(registers), t, n);
			if (add) {
				printf("+");
			}
			else {
				printf("-");
			}
			printf("%d]!\n", imm32);
		}
	}
	else {
		// wback == TRUE
		printf("  LDR%s R%d, [R%d], #",
		       get_condition_field(registers), t, n);
		if (add) {
			printf("+");
		}
		else {
			printf("-");
		}
		printf("%d\n", imm32);
	}

	if (ConditionPassed(registers)) {
		uint32_t offset_addr;
		if (add) {
			offset_addr = registers->r[n] + imm32;
		}
		else {
			offset_addr = registers->r[n] - imm32;
		}

		uint32_t address;
		if (index) {
			address = offset_addr;
		}
		else {
			address = registers->r[n];
		}

		uint32_t data = memory_word_read(address);
		if (wback) {
			registers->r[n] = offset_addr;
			printf("  > R%d = %08X\n", n, registers->r[n]);
		}

		if (t == 15) {
			assert(false);
		}
		else {
			registers->r[t] = data;
			printf("  > R%d = %08X\n", t, registers->r[t]);
		}
	}
}

static void a6_7_42_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t n    = (halfword & 0x0038) >> 3;
	uint8_t t    = (halfword & 0x0007) >> 0;

	uint32_t imm32 = imm5 << 2;
	bool index = true;
	bool add = true;
	bool wback = false;

	LDR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_42_t2(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t t    = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint8_t n    = 13;
	uint32_t imm32 = imm8 << 2;
	bool index = true;
	bool add = true;
	bool wback = false;

	LDR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_42_t3(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t  n     =  (first_halfword & 0x000F) >>  0;
	uint8_t  t     = (second_halfword & 0xF000) >> 12;
	uint16_t imm12 = (second_halfword & 0x0FFF) >>  0;

	uint32_t imm32 = imm12;
	bool index = true;
	bool add = true;
	bool wback = false;

	LDR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_42_t4(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t t    = (second_halfword & 0xF000) >> 12;
	uint8_t P    = (second_halfword & 0x0400) >> 10;
	uint8_t U    = (second_halfword & 0x0200) >>  9;
	uint8_t W    = (second_halfword & 0x0100) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	uint32_t imm32 = imm8;
	bool index = P == 1;
	bool add = U == 1;
	bool wback = W == 1;

	assert(!(wback && (n == t)));
	assert(!((t == 15)
	         && InITBlock(registers) && !LastInITBlock(registers)));

	assert(!(!index && !wback));

	LDR_immediate(registers, t, n, imm32, index, add, wback);
}

static void LDR_literal(struct registers *registers,
                        uint8_t t, uint32_t imm32, bool add)
{
	if (ConditionPassed(registers)) {
		uint32_t base = Align_PC_4(registers);
		uint32_t address;
		if (add) {
			address = base + imm32;
		}
		else {
			address = base - imm32;
		}
		uint32_t data = memory_word_read(address);
		if (t == 15) {
			assert(false);
		}
		else {
			registers->r[t] = data;
			printf("  > R%d = %08X\n", t, registers->r[t]);
		}
	}
}

static void a6_7_43_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t t = (halfword & 0x0700) >> 8;
	uint8_t imm8 = halfword;

	uint32_t imm32 = imm8 * 0x4;

	printf("  LDR R%d [PC, #%d]\n", t, imm32);
	LDR_literal(registers, t, imm32, true);
}

static void LDR_register(struct registers *registers,
                         uint8_t t, uint8_t n, uint8_t m,
                         bool index, bool add, bool wback,
                         enum SRType shift_t, uint8_t shift_n)
{
	if (ConditionPassed(registers)) {
		uint32_t offset = Shift(registers->r[m], shift_t,
		                        shift_n, APSR_C(registers));
		uint32_t offset_addr;
		if (add) {
			offset_addr = registers->r[n] + offset;
		}
		else {
			offset_addr = registers->r[n] - offset;
		}
		uint32_t address;
		if (index) {
			address = offset_addr;
		}
		else {
			address = registers->r[n];
		}
		uint32_t data = memory_word_read(address);
		if (wback) {
			registers->r[n] = offset_addr;
			printf("  > R%d = %08X\n", n, registers->r[n]);
		}

		if (t == 15) {
			assert(false);
		}
		else {
			registers->r[t] = data;
			printf("  > R%d = %08X\n", t, registers->r[t]);
		}
	}
}

static void a6_7_44_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	bool index = true;
	bool add = true;
	bool wback = false;
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	printf("  LDR%s R%d, [R%d, R%d]\n",
	       get_condition_field(registers), t, n, m);

	LDR_register(registers, t, n, m, index, add, wback, shift_t, shift_n);
}

static void a6_7_44_t2(struct registers *registers,
                       uint16_t first_halfword, uint16_t second_halfword)
{
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t t    = (second_halfword & 0xF000) >> 12;
	uint8_t imm2 = (second_halfword & 0x0030) >>  4;
	uint8_t m    = (second_halfword & 0x000F) >>  0;

	bool index = true;
	bool add = true;
	bool wback = false;
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = imm2;

	assert(!(m == 13 || m == 15));
	assert(!(t == 15 && InITBlock(registers)
	         && !LastInITBlock(registers)));

	printf("  LDR%s.W R%d, [R%d, R%d",
	       get_condition_field(registers), t, n, m);
	if (shift_n != 0) {
		printf(", LSL #%d", shift_n);
	}
	printf("]\n");

	LDR_register(registers, t, n, m, index, add, wback, shift_t, shift_n);
}

static void LDRB_immediate(struct registers *registers,
                           uint8_t t, uint8_t n, int32_t imm32,
                           bool index, bool add, bool wback)
{
	if (ConditionPassed(registers)) {
		uint32_t offset_addr;
		if (add) {
			offset_addr = registers->r[n] + imm32;
		}
		else {
			offset_addr = registers->r[n] - imm32;
		}
		uint32_t address;
		if (index) {
			address = offset_addr;
		}
		else {
			address = registers->r[n];
		}

		uint8_t data = memory_byte_read(address);
		registers->r[t] = data;
		printf("  > R%d = %08X\n", t, registers->r[t]);

		if (wback) {
			registers->r[n] = offset_addr;
			printf("  > R%d = %08X\n", n, registers->r[n]);
		}
	}
}

static void a6_7_45_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t n    = (halfword & 0x0038) >> 3;
	uint8_t t    = (halfword & 0x0007) >> 0;

	uint32_t imm32 = imm5;
	bool index = true;
	bool add = true;
	bool wback = false;

	printf("  LDRB%s R%d [R%d, #%d]\n",
	       get_condition_field(registers), t, n, imm32);

	LDRB_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_45_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t  n     =  (first_halfword & 0x000F) >>  0;
	uint8_t  t     = (second_halfword & 0xF000) >> 12;
	uint16_t imm12 = (second_halfword & 0x0FFF) >>  0;

	uint32_t imm32 = imm12;
	bool index = true;
	bool add = true;
	bool wback = false;

	assert(!(t == 13));

	printf("  LDRB%s.W R%d, [R%d, #%d]\n",
	       get_condition_field(registers), t, n, imm32);

	LDRB_immediate(registers, t, n, imm32, index, add, wback);
}

static void LDRB_register(struct registers *registers,
                          uint8_t t, uint8_t n, uint8_t m,
                          bool index, bool add, bool wback,
                          enum SRType shift_t, uint8_t shift_n)
{
	assert(shift_t == SRType_LSL);
	assert(shift_n == 0);

	if (ConditionPassed(registers)) {
		uint32_t offset = registers->r[m];
		uint32_t offset_addr;
		if (add) {
			offset_addr = registers->r[n] + offset;
		}
		else {
			offset_addr = registers->r[n] - offset;
		}
		uint32_t address;
		if (index) {
			address = offset_addr;
		}
		else {
			address = registers->r[n];
		}
		uint32_t data = memory_byte_read(address); // ZeroExtend

		registers->r[t] = data;
		printf("  > R%d = %08X\n", t, registers->r[t]);
	}
}

static void a6_7_47_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	bool index = true;
	bool add = true;
	bool wback = false;

	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	printf("  LDRB%s R%d [R%d, R%d]\n",
	       get_condition_field(registers), t, n, m);
	LDRB_register(registers, t, n, m, index, add, wback, shift_t, shift_n);
}

static void LSL_immediate(struct registers *registers,
                          uint8_t d, uint8_t m, bool setflags, uint8_t shift_n)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryTuple T =
			Shift_C(registers->r[m], SRType_LSL,
			        shift_n, APSR_C(registers));
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_67_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t m = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;

	bool setflags = !InITBlock(registers);
	struct ShiftTNTuple T = DecodeImmShift(0b00, imm5);
	uint8_t shift_n = T.shift_n;

	printf("  LSL");
	if (setflags) {
		printf("S");
	}
	printf(" R%d, R%d, #%d\n", d, m, shift_n);

	LSL_immediate(registers, d, m, setflags, shift_n);
}

static void LSL_register(struct registers *registers,
                          uint8_t d, uint8_t n, uint8_t m, bool setflags)
{
	if (ConditionPassed(registers)) {
		uint8_t shift_n = (registers->r[m] & 0x000000FF);
		struct ResultCarryTuple T =
			Shift_C(registers->r[n], SRType_LSL,
			        shift_n, APSR_C(registers));
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_68_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t S =  (first_halfword & 0x0010) >> 4;
	uint8_t n =  (first_halfword & 0x000F) >> 0;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t m = (second_halfword & 0x000F) >> 0;

	bool setflags = S == 1;

	assert(!(d == 13 || d == 15 || n == 13 || n == 15));

	printf("  LSL");
	if (setflags) {
		printf("S");
	}
	printf("%s.W R%d, R%d, R%d\n",
	       get_condition_field(registers), d, n, m);

	LSL_register(registers, d, n, m, setflags);
}

static void LSR_immediate(struct registers *registers,
                          uint8_t d, uint8_t m, bool setflags, uint8_t shift_n)
{
	printf("  LSR");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, R%d, #%d\n",
	       get_condition_field(registers), d, m, shift_n);

	if (ConditionPassed(registers)) {
		struct ResultCarryTuple T =
			Shift_C(registers->r[m], SRType_LSR, shift_n,
			        APSR_C(registers));
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);

		if (setflags) {
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_69_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t m    = (halfword & 0x0038) >> 3;
	uint8_t d    = (halfword & 0x0007) >> 0;

	bool setflags = !InITBlock(registers);

	struct ShiftTNTuple T = DecodeImmShift(0b01, imm5);
	uint8_t shift_n = T.shift_n;

	LSR_immediate(registers, d, m, setflags, shift_n);
}

static void a6_7_69_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm2 = (second_halfword & 0x00C0) >>  6;
	uint8_t m    = (second_halfword & 0x000F) >>  0;

	bool setflags = S == 1;

	uint8_t imm5 = (imm3 << 2) | imm2;
	struct ShiftTNTuple T = DecodeImmShift(0b01, imm5);
	uint8_t shift_n = T.shift_n;

	assert(!(d == 13 || d == 15 || m == 13 || m == 15));

	LSR_immediate(registers, d, m, setflags, shift_n);
}

static void a6_7_73_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t a = (second_halfword & 0xF000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t m = (second_halfword & 0x000F) >> 0;

	uint64_t result = registers->r[n] * registers->r[m] + registers->r[a];

	printf("  MLA R%d, R%d, R%d, R%d\n", d, n, m, a);
	registers->r[d] = (result & 0xFFFFFFFF);
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_74_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t a = (second_halfword & 0xF000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t m = (second_halfword & 0x000F) >> 0;

	uint64_t result = registers->r[a] - registers->r[n] * registers->r[m];

	printf("  MLS R%d, R%d, R%d, R%d\n", d, n, m, a);
	registers->r[d] = (result & 0xFFFFFFFF);
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void MOV_immediate(struct registers *registers,
                          uint8_t d,
                          bool setflags,
                          uint32_t imm32,
                          bool carry)
{
	if (ConditionPassed(registers)) {
		uint32_t result = imm32;
		registers->r[d] = imm32;
		printf("  > R%d = %08X\n", d, imm32);
		if (setflags) {
			if ((result & 0x80000000) == 0x80000000) {
				APSR_N_set(registers);
			}
			else {
				APSR_N_clear(registers);
			}
			if (result == 0) {
				APSR_Z_set(registers);
			}
			else {
				APSR_Z_clear(registers);
			}
			if (carry) {
				APSR_C_set(registers);
			}
			else {
				APSR_C_clear(registers);
			}
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_75_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t d = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	bool setflags;
	if (InITBlock(registers)) {
		setflags = false;
		printf("  MOV%s R%d, #%d\n",
		       get_condition_field(registers), d, imm8);
	}
	else {
		setflags = true;
		printf("  MOVS R%d, #%d\n", d, imm8);
	}
	uint32_t imm32 = imm8;
	bool carry = APSR_C(registers);

	MOV_immediate(registers, d, setflags, imm32, carry);
}

static void a6_7_75_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i    =  (first_halfword & 0x0400) >> 10;
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	bool setflags = S == 1;
	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;

	struct ResultCarryTuple T = ThumbExpandImm_C(imm12, false);
	uint32_t imm32 = T.result;
	bool carry = T.result;

	printf("  MOV");
	if (setflags) {
		printf("S");
	}
	printf("%s.W R%d #0x%08X\n",
	       get_condition_field(registers), d, T.result);

	MOV_immediate(registers, d, setflags, imm32, carry);
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

static void a6_7_76_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t D  = (halfword & 0x0080) >> 7;
	uint8_t m  = (halfword & 0x0078) >> 3;
	uint8_t Rd = (halfword & 0x0007) >> 0;

	uint8_t d = (D << 3) | Rd;
	bool setflags = false;

	uint32_t result = registers->r[m];
	assert(d != 15);

	printf("  MOV%s R%d, R%d\n", get_condition_field(registers), d, m);

	if (ConditionPassed(registers)) {
		registers->r[d] = result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
	}
}

static void a6_7_78_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t imm4 = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint16_t imm16 = (imm4 << 12)
	                 + (i << 11)
	                 + (imm3 << 8)
	                 + imm8;

	printf("  MOVT%s R%d, #0x%04X\n",
	       get_condition_field(registers), d, imm16);

	registers->r[d] &= 0xFFFF;
	registers->r[d] |= imm16 << 16;
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_84_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	bool setflags = S == 1;

	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;

	struct ResultCarryTuple T = ThumbExpandImm_C(imm12, APSR_C(registers));
	uint32_t imm32 = T.result;

	printf("  MVN");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, #0x%08X\n",
	       get_condition_field(registers), d, imm32);

	if (ConditionPassed(registers)) {
		uint32_t result = ~imm32;
		registers->r[d] = result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			T.result = result;
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_87_t1(struct registers *registers, uint16_t halfword)
{
	printf("  NOP\n");
}

static void a6_7_89_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	assert(false);
}

static void a6_7_90_t1(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint16_t imm12 = (i << 11) | (imm3 << 8) | imm8;

	struct ResultCarryTuple T = ThumbExpandImm_C(imm12, APSR_C(registers));
	uint32_t imm32 = T.result;

	bool setflags = S == 1;

	printf("  ORR");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, R%d, #0x%08X\n",
	       get_condition_field(registers), d, n, imm32);

	if (ConditionPassed(registers)) {
		uint32_t result = registers->r[n] | imm32;
		registers->r[d] = result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			T.result = result;
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void ORR_register(struct registers *registers,
                         uint8_t d, uint8_t n, uint8_t m, bool setflags,
                         enum SRType shift_t, uint8_t shift_n)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryTuple T =
			Shift_C(registers->r[m], shift_t, shift_n,
			        APSR_C(registers));
		uint32_t shifted = T.result;
		uint32_t result = registers->r[m] | shifted;
		registers->r[d] = result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			T.result = result;
			setflags_ResultCarryTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_91_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t m  = (halfword & 0x0038) >>  3;
	uint8_t dn = (halfword & 0x0007) >>  0;

	uint8_t d = dn;
	uint8_t n = dn;
	bool setflags = !InITBlock(registers);
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	printf("  ORR");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, R%d\n",
	       get_condition_field(registers), dn, m);

	ORR_register(registers, d, n, m, setflags, shift_t, shift_n);
}

static void a6_7_91_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm2 = (second_halfword & 0x00C0) >>  6;
	uint8_t type = (second_halfword & 0x0030) >>  4;
	uint8_t m    = (second_halfword & 0x000F) >>  0;

	bool setflags = S == 1;
	uint8_t imm5 = (imm3 << 2) | imm2;
	struct ShiftTNTuple T = DecodeImmShift(type, imm5);
	enum SRType shift_t = T.shift_t;
	uint8_t shift_n = T.shift_n;

	assert(!(d == 13 || d == 15 || n == 13 || m == 13 || m == 15));

	printf("  ORR");
	if (setflags) {
		printf("S");
	}
	printf("%s.W R%d, R%d, R%d",
	       get_condition_field(registers), d, n, m);
	if (shift_n != 0) {
		printf(", <shift>");
	}
	printf("\n");

	ORR_register(registers, d, n, m, setflags, shift_t, shift_n);
}

static void POP(struct registers *registers,
                uint16_t all_registers)
{
	if (ConditionPassed(registers)) {
		uint32_t address = SP(registers);
		for (uint8_t i = 0; i < 15; ++i) {
			if ((all_registers & (0x0001 << i)) == (0x0001 << i)) {
				registers->r[i] = memory_word_read(address);
				printf("  > R%d = %08X (MemA[%08X, 4])\n",
				       i, registers->r[i], address);
				address += 4;
			}
		}
		if ((all_registers & (0x0001 << 15)) == (0x0001 << 15)) {
			uint32_t arg = memory_word_read(address);
			LoadWritePC(registers, arg);
		}
		uint8_t bit_count = __builtin_popcount(all_registers);
		address = registers->r[13] + 4 * bit_count;
		registers->r[13] = address;
		printf("  > R13 = %08X\n", address);
	}
}

static void a6_7_97_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t register_list = (halfword & 0x00FF) >> 0;
	uint8_t P = (halfword & 0x0100) >> 8;

	uint16_t all_registers = (P << 15) | register_list;
	uint8_t bit_count = __builtin_popcount(all_registers);

	printf("  POP {");
	bool first = false;
	for (uint8_t i = 0; i < 16; ++i) {
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

	POP(registers, all_registers);
}

static void a6_7_97_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t P = (second_halfword & 0x8000) >> 15;
	uint8_t M = (second_halfword & 0x4000) >> 14;
	uint16_t register_list = (second_halfword & 0x1FFF) >> 0;

	uint16_t all_registers = (P << 15) | (M << 14) | register_list;

	printf("  POP.W {");
	bool first = false;
	for (uint8_t i = 0; i < 16; ++i) {
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

	uint8_t bit_count = __builtin_popcount(all_registers);
	if ((bit_count < 2) || ((P == 1) && (M == 1))) {
		assert(false);
	}

	POP(registers, all_registers);
}

static void PUSH(struct registers *registers,
                 uint16_t all_registers)
{
	printf("  PUSH%s {", get_condition_field(registers));
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

	if (ConditionPassed(registers)) {
		uint8_t bit_count = __builtin_popcount(all_registers);
		uint32_t address = registers->r[13] - 4 * bit_count;
		printf("  > Note: higher registers at higher addresses\n");
		for (uint8_t i = 0; i < 15; ++i) {
			if ((all_registers & (0x0001 << i)) == (0x0001 << i)) {
				memory_word_write(address & 0xFFFFFFFC, registers->r[i]);
				address += 4;
			}
		}
		address = registers->r[13] - 4 * bit_count;
		registers->r[13] = address;
		printf("  > R13 = %08X\n", address);
	}
}

static void a6_7_98_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t register_list = (halfword & 0x00FF) >> 0;
	uint8_t M = (halfword & 0x0100) >> 8;

	uint16_t all_registers = (M << 14) | register_list;

	PUSH(registers, all_registers);
}

static void a6_7_98_t2(struct registers *registers,
                       uint16_t first_halfword,
                       uint16_t second_halfword)
{
	uint8_t  M             = (second_halfword & 0x4000) >> 14;
	uint16_t register_list = (second_halfword & 0x1FFF) >>  0;

	uint16_t all_registers = (M << 14) | register_list;

	PUSH(registers, all_registers);
}

static void RSB_immediate(struct registers *registers,
                          uint8_t d, uint8_t n, bool setflags, uint32_t imm32)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryOverflowTuple T =
			AddWithCarry(~(registers->r[n]), imm32, true);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_106_t2(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t i    =  (first_halfword & 0x0400) >> 10;
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;

	bool setflags = S == 1;
	uint32_t imm32 = ThumbExpandImm(registers, imm12);

	printf("  RSB");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, R%d, #0x%08X\n",
	       get_condition_field(registers), d, n, imm32);

	RSB_immediate(registers, d, n, setflags, imm32);
}

static void RSB_register(struct registers *registers,
                         uint8_t d, uint8_t n, int8_t m,
                         bool setflags, enum SRType shift_t, uint8_t shift_n)
{
	if (ConditionPassed(registers)) {
		uint32_t shifted = Shift(registers->r[m],
		                         shift_t, shift_n, APSR_C(registers));
		struct ResultCarryOverflowTuple T =
			AddWithCarry(~(registers->r[n]), shifted, true);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_107_t1(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t S    =  (first_halfword & 0x0010) >>  4;
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d    = (second_halfword & 0x0F00) >>  8;
	uint8_t imm2 = (second_halfword & 0x00C0) >>  6;
	uint8_t type = (second_halfword & 0x0030) >>  4;
	uint8_t m    = (second_halfword & 0x000F) >>  0;

	bool setflags = S == 1;
	uint8_t imm5 = (imm3 << 2) | imm2;
	struct ShiftTNTuple T = DecodeImmShift(type, imm5);

	printf("  RSB");
	if (setflags) {
		printf("S");
	}
	printf("%s R%d, R%d, R%d",
	       get_condition_field(registers), d, n, m);
	if (T.shift_n != 0) {
		printf(", <shift>");
	}
	printf("\n");

	assert(!(d == 13 || d == 15 || n == 13 || n == 15));

	RSB_register(registers, d, n, m, setflags, T.shift_t, T.shift_n);
}

static void STR_immediate(struct registers *registers,
                          uint8_t t, uint8_t n, int32_t imm32,
                          bool index, bool add, bool wback)
{
	if (index) {
		if (!wback) {
			printf("  STR%s R%d, [R%d, #%d]\n",
			       get_condition_field(registers), t, n, imm32);
		}
		else {
			printf("  STR%s R%d, [R%d, #%d]!\n",
			       get_condition_field(registers), t, n, imm32);
		}
	}
	else {
		printf("  STR%s R%d, [R%d], #%d\n",
		       get_condition_field(registers), t, n, imm32);
	}

	if (ConditionPassed(registers)) {
		uint32_t offset_addr;
		if (add) { offset_addr = registers->r[n] + imm32; }
		else     { offset_addr = registers->r[n] - imm32; }
		uint32_t address;
		if (index) { address = offset_addr; }
		else       { address = registers->r[n]; }

		memory_word_write(address, registers->r[t]);
		if (wback) {
			printf("  > R%d = %08X\n", n, offset_addr);
			registers->r[n] = offset_addr;
		}
	}
}

static void a6_7_119_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	uint32_t imm32 = imm5 << 2;
	bool index = true;
	bool add = true;
	bool wback = false;

	STR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_119_t2(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t t = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint8_t n = 13;
	uint32_t imm32 = (imm8 << 2);

	bool index = true;
	bool add = true;
	bool wback = false;

	STR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_119_t3(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t  n     =  (first_halfword & 0x000F) >>  0;
	uint8_t  t     = (second_halfword & 0xF000) >> 12;
	uint16_t imm12 = (second_halfword & 0x0FFF) >>  0;

	uint32_t imm32 = imm12;

	bool index = true;
	bool add = true;
	bool wback = false;

	assert(!(t == 15));

	STR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_119_t4(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n =  (first_halfword & 0x000F) >>  0;
	uint8_t t = (second_halfword & 0xF000) >> 12;
	uint8_t P = (second_halfword & 0x0400) >> 10;
	uint8_t U = (second_halfword & 0x0200) >>  9;
	uint8_t W = (second_halfword & 0x0100) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;
	bool index = P == 1;
	bool add = U == 1;
	bool wback = W == 1;

	STR_immediate(registers, t, n, imm32, index, add, wback);
}

static void STR_register(struct registers *registers,
                         uint8_t t, uint8_t n, uint8_t m,
                         bool index, bool add, bool wback,
                         enum SRType shift_t, uint8_t shift_n)
{
	if (ConditionPassed(registers)) {
		uint32_t offset = Shift(registers->r[m], shift_t,
		                        shift_n, APSR_C(registers));
		uint32_t address = registers->r[n] + offset;
		uint32_t data = registers->r[t];
		memory_word_write(address, data);
	}
}

static void a6_7_120_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	bool index = true;
	bool add = true;
	bool wback = false;

	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	printf("  STR%s R%d, [R%d, R%d]\n",
	       get_condition_field(registers), t, n, m);

	STR_register(registers, t, n, m, index, add, wback, shift_t, shift_n);
}

static void a6_7_120_t2(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t t    = (second_halfword & 0xF000) >> 12;
	uint8_t imm2 = (second_halfword & 0x0030) >>  4;
	uint8_t m    = (second_halfword & 0x000F) >>  0;

	bool index = true;
	bool add = true;
	bool wback = false;

	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = imm2;

	printf("  STR%s R%d, [R%d, R%d",
	       get_condition_field(registers), t, n, m);
	if (shift_n != 0) {
		printf(", LSL #%d", imm2);
	}
	printf("]\n");

	STR_register(registers, t, n, m, index, add, wback, shift_t, shift_n);
}

static void STRB(struct registers *registers,
                 uint8_t t, uint8_t n, uint32_t imm32,
                 bool index, bool add, bool wback)
{
	uint32_t offset_addr;
	if (add) { offset_addr = registers->r[n] + imm32; }
	else     { offset_addr = registers->r[n] - imm32; }
	uint32_t address;
	if (index) { address = offset_addr; }
	else       { address = registers->r[n]; }

	if (index) {
		if (!wback) {
			printf("  STRB R%d [R%d, #%d]\n", t, n, imm32);
		}
		else {
			printf("  STRB R%d [R%d, #%d]!\n", t, n, imm32);
		}
	}
	else {
		printf("  STRB R%d [R%d] #%d\n", t, n, imm32);
	}

	uint8_t value = registers->r[t] & 0x000000FF;
	memory_byte_write(address, value);

	if (wback) {
		registers->r[n] = offset_addr;
		printf("  > R%d = %08X\n", n, offset_addr);
	}
}

static void a6_7_121_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	uint32_t imm32 = imm5;
	bool index = true;
	bool add = true;
	bool wback = false;

	STRB(registers, t, n, imm32, index, add, wback);
}

static void a6_7_121_t2(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t  n     =  (first_halfword & 0x000F) >>  0;
	uint8_t  t     = (second_halfword & 0xF000) >> 12;
	uint16_t imm12 = (second_halfword & 0x0FFF) >>  0;

	uint32_t imm32 = imm12;
	bool index = true;
	bool add = true;
	bool wback = false;

	assert(!(n == 15)); // UNDEFINED
	assert(!(t == 13 || t == 15)); // UNPREDICTABLE

	STRB(registers, t, n, imm32, index, add, wback);
}

static void a6_7_121_t3(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n    =  (first_halfword & 0x000F) >>  0;
	uint8_t t    = (second_halfword & 0xF000) >> 12;
	uint8_t P    = (second_halfword & 0x0400) >> 10;
	uint8_t U    = (second_halfword & 0x0200) >>  9;
	uint8_t W    = (second_halfword & 0x0100) >>  8;
	uint8_t imm8 = (second_halfword & 0x00FF) >>  0;

	uint32_t imm32 = imm8;
	bool index = P == 1;
	bool add = U == 1;
	bool wback = W == 1;

	STRB(registers, t, n, imm32, index, add, wback);
}

static void STRB_register(struct registers *registers,
                          uint8_t t, uint8_t n, uint8_t m,
                          bool index, bool add, bool wback,
                          enum SRType shift_t, uint8_t shift_n)
{
	assert(shift_t == SRType_LSL);
	assert(shift_n == 0);

	if (ConditionPassed(registers)) {
		uint32_t offset = registers->r[m];
		uint32_t address = registers->r[n] + offset;
		memory_byte_write(address, registers->r[t]);
	}
}

static void a6_7_122_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	bool index = true;
	bool add = true;
	bool wback = false;
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	printf("  STRB%s R%d, [R%d, R%d]\n",
	       get_condition_field(registers), t, n, m);
	STRB_register(registers, t, n, m, index, add, wback, shift_t, shift_n);
}

static void a6_7_128_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t rt = (halfword & 0x0007);
	uint8_t rn = (halfword & 0x0038) >> 3;
	uint8_t imm5 = (halfword & 0x07C0) >> 6;

	uint32_t imm32 = imm5 << 1;

	uint32_t offset_addr = registers->r[rn] + imm32;
	uint32_t address = offset_addr;

	uint16_t value = registers->r[rt] & 0x0000FFFF;

	printf("  STRH R%d [R%d, #%d]\n", rt, rn, imm32);

	memory_halfword_write(address, value);
}

static void SUB_immediate(struct registers *registers,
                          uint8_t d, uint8_t n, bool setflags, uint32_t imm32)
{
	if (ConditionPassed(registers)) {
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[n], ~imm32, true);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_132_t2(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t d = (halfword & 0x0700) >> 8;
	uint8_t n = d;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;

	bool setflags = !InITBlock(registers);

	if (setflags) {
		printf("  SUBS R%d, R%d, #%d\n", d, n, imm32);
	}
	else {
		printf("  SUB R%d, R%d, #%d\n", d, n, imm32);
	}

	SUB_immediate(registers, d, n, setflags, imm32);
}

static void SUB_register(struct registers *registers,
                         uint8_t d, uint8_t n, uint8_t m, bool setflags,
                         enum SRType shift_t, uint8_t shift_n)
{
	if (ConditionPassed(registers)) {
		uint32_t shifted = Shift(registers->r[m], shift_t,
		                         shift_n, APSR_C(registers));
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[n], ~shifted, true);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_133_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;

	bool setflags = !InITBlock(registers);
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	printf("  SUB");
	if (setflags) {
		printf("S");
	}
	printf(" R%d, R%d, R%d\n", d, n, m);

	SUB_register(registers, d, n, m, setflags, shift_t, shift_n);
}

static void a6_7_134_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t imm7 = (halfword & 0x007F) >> 0;

	uint8_t d = 13;
	bool setflags = false;

	uint32_t imm32 = (imm7 << 2);

	printf("  SUB%s SP, SP, #%d\n", get_condition_field(registers), imm7);

	if (ConditionPassed(registers)) {
		struct ResultCarryOverflowTuple T =
			AddWithCarry(registers->r[d], ~imm32, true);
		registers->r[d] = T.result;
		printf("  > R%d = %08X\n", d, registers->r[d]);
		if (setflags) {
			setflags_ResultCarryOverflowTuple(registers, T);
			printf("  > APSR = %08X\n", registers->apsr);
		}
	}
}

static void a6_7_144_t1(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm2 = (second_halfword & 0x00C0) >> 6;
	uint8_t widthminus1 = (second_halfword & 0x001F) >> 0;

	uint8_t lsbit = (imm3 << 2) | imm2;

	printf("  UBFX%s R%d, R%d, #%d, #%d\n",
	       get_condition_field(registers), d, n, lsbit, widthminus1 + 1);

	assert(!(d == 13 || d == 15 || n == 13 || n == 15));

	if (ConditionPassed(registers)) {
		uint8_t msbit = lsbit + widthminus1;
		if (msbit <= 31) {
			registers->r[d] = 0;
			for (uint8_t i = 0; i < 32; ++i) {
				if (i >= lsbit && i <= msbit) {
					registers->r[d] |= (registers->r[n] & (1 << i)) >> lsbit;
				}
			}
			printf("  > R%d = %08X\n", d, registers->r[d]);
		}
		else {
			assert(false);
		}
	}
}

static void a6_7_145_t1(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t m = (second_halfword & 0x000F) >> 0;

	assert(registers->r[m] != 0);

	printf("  UDIV R%d, R%d, R%d\n", d, n, m);
	registers->r[d] = registers->r[n] / registers->r[m];
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_149_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t rm = (halfword & 0x0038) >> 3;
	uint8_t rd = (halfword & 0x0007) >> 0;

	uint32_t value = (registers->r[rm] & 0x000000FF);

	printf("  UXTB R%d, R%d\n", rd, rm);
	registers->r[rd] = value;
	printf("  > R%d = %08X\n", rd, registers->r[rd]);
}

static void a5_2_1(struct registers *registers,
                   uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x3E00) >> 9;

	if ((opcode & 0b11100) == 0b00000) {
		a6_7_67_t1(registers, halfword); // LSL
	}
	else if ((opcode & 0b11100) == 0b00100) {
		a6_7_69_t1(registers, halfword); // LSR
	}
	else if ((opcode & 0b11100) == 0b01000) {
		a6_7_10_t1(registers, halfword); // ASR
	}
	else if (opcode == 0b01100) {
		a6_7_4_t1(registers, halfword);
	}
	else if (opcode == 0b01101) {
		a6_7_133_t1(registers, halfword); // SUB
	}
	else if (opcode == 0b01110) {
		a6_7_3_t1(registers, halfword); // ADD
	}
	else if (opcode == 0b01111) {
		printf("  SUB?\n");
		assert(false);
	}
	else if ((opcode & 0b11100) == 0b10000) {
		a6_7_75_t1(registers, halfword);
	}
	else if ((opcode & 0b11100) == 0b10100) {
		a6_7_27_t1(registers, halfword);
	}
	else if ((opcode & 0b11100) == 0b11000) {
		a6_7_3_t2(registers, halfword); // ADD
	}
	else if ((opcode & 0b11100) == 0b11100) {
		a6_7_132_t2(registers, halfword); // SUB
	}
	else {
		assert(false);
	}
}

static void a5_2_2(struct registers *registers,
                   uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x03C0) >> 6;
	switch (opcode) {
	case 0b0000:
		printf("  AND? a5_2_2\n");
		assert(false);
		break;
	case 0b0001:
		printf("  EOR? a5_2_2\n");
		assert(false);
		break;
	case 0b0010:
		printf("  LSL? a5_2_2\n");
		assert(false);
		break;
	case 0b0011:
		printf("  LSR? a5_2_2\n");
		assert(false);
		break;
	case 0b0100:
		printf("  ASR? a5_2_2\n");
		assert(false);
		break;
	case 0b0101:
		printf("  ADC? a5_2_2\n");
		assert(false);
		break;
	case 0b0110:
		printf("  SBC? a5_2_2\n");
		assert(false);
		break;
	case 0b0111:
		printf("  ROR? a5_2_2\n");
		assert(false);
		break;
	case 0b1000:
		printf("  TST? a5_2_2\n");
		assert(false);
		break;
	case 0b1001:
		printf("  RSB? a5_2_2\n");
		assert(false);
		break;
	case 0b1010:
		a6_7_28_t1(registers, halfword); // CMP
		break;
	case 0b1011:
		printf("  CMN? a5_2_2\n");
		assert(false);
		break;
	case 0b1100:
		a6_7_91_t1(registers, halfword); // ORR (register)
		break;
	case 0b1101:
		printf("  MUL? a5_2_2\n");
		assert(false);
		break;
	case 0b1110:
		printf("  BIC? a5_2_2\n");
		assert(false);
		break;
	case 0b1111:
		printf("  MVN? a5_2_2\n");
		assert(false);
		break;
	}
}

static void a5_2_3(struct registers *registers,
                   uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x03C0) >> 6;
	if ((opcode & 0b1100) == 0b0000) {
		a6_7_4_t2(registers, halfword); // ADD
	}
	else if (opcode == 0b0100) {
		assert(false);
	}
	else if (opcode == 0b0101) {
		a6_7_28_t2(registers, halfword); // CMP
	}
	else if ((opcode & 0b1110) == 0b0110) {
		a6_7_28_t2(registers, halfword); // CMP
	}
	else if ((opcode & 0b1100) == 0b1000) {
		a6_7_76_t1(registers, halfword); // MOV
	}
	else if ((opcode & 0b1110) == 0b1100) {
		a6_7_20_t1(registers, halfword); // BX
	}
	else if ((opcode & 0b1110) == 0b1110) {
		a6_7_19_t1(registers, halfword); // BLX
	}
	else {
		assert(false);
	}
}

static void a5_2_4(struct registers *registers,
                   uint16_t halfword)
{
	uint8_t opA = (halfword & 0xF000) >> 12;
	uint8_t opB = (halfword & 0x0E00) >> 9;

	if (opA == 0b0101) {
		switch (opB) {
		case 0b000:
			a6_7_120_t1(registers, halfword); // STR
			break;
		case 0b001:
			printf("  STRH? a5_2_4\n");
			assert(false);
			break;
		case 0b010:
			a6_7_122_t1(registers, halfword); // STRB
			break;
		case 0b011:
			printf("  LDRSB? a5_2_4\n");
			assert(false);
			break;
		case 0b100:
			a6_7_44_t1(registers, halfword); // LDR
			break;
		case 0b101:
			printf("  LDRH? a5_2_4\n");
			assert(false);
			break;
		case 0b110:
			a6_7_47_t1(registers, halfword); // LDRB
			break;
		case 0b111:
			printf("  LDRSH? a5_2_4\n");
			assert(false);
			break;
		default:
			assert(false);
		}
	}
	else if (opA == 0b0110) {
		if ((opB & 0x4) == 0x0) {
			a6_7_119_t1(registers, halfword);
		}
		else {
			a6_7_42_t1(registers, halfword);
		}
	}
	else if (opA == 0b0111) {
		if ((opB & 0b100) == 0b000) {
			a6_7_121_t1(registers, halfword);
		}
		else {
			a6_7_45_t1(registers, halfword);
		}
	}
	else if (opA == 0b1000) {
		if ((opB & 0b100) == 0b000) {
			a6_7_128_t1(registers, halfword);
		}
		else {
			assert(false);
		}
	}
	else if (opA == 0b1001) {
		if ((opB & 0b100) == 0b000) {
			a6_7_119_t2(registers, halfword); // STR
		}
		else if ((opB & 0b100) == 0b100) {
			a6_7_42_t2(registers, halfword); // LDR
		}
	}
	else {
		assert(false);
	}
}

static void a5_2_5(struct registers *registers, uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x0FE0) >> 5;
	if (opcode == 0b0110011) {
		b4_1_1_t1(registers, halfword); // CPS
	}
	else if ((opcode & 0b1111100) == 0b0000000) {
		a6_7_5_t2(registers, halfword); // ADD
	}
	else if ((opcode & 0b1111100) == 0b0000100) {
		a6_7_134_t1(registers, halfword); // SUB
	}
	else if ((opcode & 0b1111000) == 0b0001000) {
		a6_7_21_t1(registers, halfword); // CBNZ, CBZ
	}
	else if ((opcode & 0b1111110) == 0b0010000) {
		printf("  SXTH? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111110) == 0b0010010) {
		printf("  SXTB? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111110) == 0b0010100) {
		printf("  UXTH? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111110) == 0b0010110) {
		a6_7_149_t1(registers, halfword); // UXTB
	}
	else if ((opcode & 0b1111000) == 0b0011000) {
		a6_7_21_t1(registers, halfword); // CBNZ, CBZ
	}
	else if ((opcode & 0b1110000) == 0b0100000) {
		a6_7_98_t1(registers, halfword); // PUSH
	}
	else if ((opcode & 0b1111000) == 0b1001000) {
		a6_7_21_t1(registers, halfword); // CBNZ, CBZ
	}
	else if ((opcode & 0b1111110) == 0b1010000) {
		printf("  REV? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111110) == 0b1010010) {
		printf("  REV16? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111110) == 0b1010110) {
		printf("  REVSH? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111000) == 0b1011000) {
		a6_7_21_t1(registers, halfword); // CBNZ, CBZ
	}
	else if ((opcode & 0b1110000) == 0b1100000) {
		a6_7_97_t1(registers, halfword); // POP
	}
	else if ((opcode & 0b1111000) == 0b1110000) {
		printf("  BKPT? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111000) == 0b1111000) {
		uint8_t opA = (halfword & 0x00F0) >> 4;
		uint8_t opB = (halfword & 0x000F);

		if (opB != 0b0000) {
			a6_7_37_t1(registers, halfword); // IT
		}
		else if (opA == 0b0000) {
			a6_7_87_t1(registers, halfword); // NOP
		}
		else if (opA == 0b0001) {
			printf("  YIELD a5_2_5\n");
			assert(false);
		}
		else if (opA == 0b0010) {
			printf("  WFE a5_2_5\n");
			assert(false);
		}
		else if (opA == 0b0011) {
			printf("  WFI a5_2_5\n");
			assert(false);
		}
		else if (opA == 0b0100) {
			printf("  SEV a5_2_5\n");
			assert(false);
		}
	}
}

static void a5_2_6(struct registers *registers, uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x0F00) >> 8;
	if (!((opcode & 0xE) == 0xE)) {
		a6_7_12_t1(registers, halfword);
	}
	else if (opcode == 0xE) {
		assert(false); // UNDEFINED
	}
	else if (opcode == 0xF) {
		assert(false);
	}
	else {
		assert(false);
	}
}

/* 16-bit instruction encoding */
static void a5_2(struct registers *registers, uint16_t halfword)
{
	printf("%08X: %04X\n", registers->r[15], halfword);

	uint8_t opcode = (halfword & 0xFC00) >> 10;

	// Shift (immediate), add, subtract, move and compare
	if ((opcode & 0b110000) == 0b000000) {
		a5_2_1(registers, halfword);
	}
	// Data processing
	else if (opcode == 0b010000) {
		a5_2_2(registers, halfword);
	}
	// Special data instructions and branch and exchange
	else if (opcode == 0b010001) {
		a5_2_3(registers, halfword);
	}
	// Load from Literal Pool
	else if ((opcode & 0b111110) == 0b010010) {
		a6_7_43_t1(registers, halfword);
	}
	// Load/store single data item
	else if ((opcode & 0b111100) == 0b010100) {
		a5_2_4(registers, halfword);
	}
	else if ((opcode & 0b111000) == 0b011000) {
		a5_2_4(registers, halfword);
	}
	else if ((opcode & 0b111000) == 0b100000) {
		a5_2_4(registers, halfword);
	}
	else if ((opcode & 0b111110) == 0b101000) {
		printf("  ADR? a5_2\n");
		assert(false);
	}
	else if ((opcode & 0b111110) == 0b101010) {
		a6_7_5_t1(registers, halfword); // ADD
	}
	else if ((opcode & 0b111100) == 0b101100) {
		a5_2_5(registers, halfword);
	}
	else if ((opcode & 0b111100) == 0b110100) {
		a5_2_6(registers, halfword);
	}
	else if ((opcode & 0b111110) == 0b111000) {
		a6_7_12_t2(registers, halfword);
	}
}

static void a5_3_1(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op = (first_halfword & 0x01F0) >> 4;
	uint8_t rn = (first_halfword & 0x000F) >> 0;
	uint8_t rd = (second_halfword & 0x0F00) >> 8;

	if ((op & 0b11110) == 0b00000) {
		if (!(rd == 0b1111)) {
			a6_7_8_t1(registers,
			          first_halfword, second_halfword); // AND
		}
		else if (rd == 0b1111) {
			printf("  TST? a5_3_1\n");
			assert(false);
		}
	}
	else if ((op & 0b11110) == 0b00010) {
		a6_7_15_t1(registers, first_halfword, second_halfword); // BIC
	}
	else if ((op & 0b11110) == 0b00100) {
		if (!(rn == 0b1111)) {
			a6_7_90_t1(registers,
			           first_halfword, second_halfword); // ORR
		}
		else if (rn == 0b1111) {
			a6_7_75_t2(registers,
			           first_halfword, second_halfword); // MOV
		}
	}
	else if ((op & 0b11110) == 0b00110) {
		if (!(rn == 0b1111)) {
			printf("  ORN? a5_3_1\n");
			assert(false);
		}
		else if (rn == 0b1111) {
			a6_7_84_t1(registers,
			           first_halfword, second_halfword); // MVN
		}
	}
	else if ((op & 0b11110) == 0b01000) {
		if (!(rd == 0b1111)) {
			printf("  EOR? a5_3_1\n");
			assert(false);
		}
		else if (rd == 0b1111) {
			printf("  TEQ? a5_3_1\n");
			assert(false);
		}
	}
	else if ((op & 0b11110) == 0b10000) {
		if (!(rd == 0b1111)) {
			a6_7_3_t3(registers,
			          first_halfword, second_halfword); // ADD
		}
		else if (rd == 0b1111) {
			printf("  CMN? a5_3_1\n");
			assert(false);
		}
	}
	else if ((op & 0b11110) == 0b10100) {
		printf("  ADC? a5_3_1\n");
		assert(false);
	}
	else if ((op & 0b11110) == 0b10110) {
		printf("  SBC? a5_3_1\n");
		assert(false);
	}
	else if ((op & 0b11110) == 0b11010) {
		if (!(rd == 0b1111)) {
			printf("  SUB? a5_3_1\n");
			assert(false);
		}
		else if (rd == 0b1111) {
			a6_7_27_t2(registers,
			           first_halfword, second_halfword); // CMP
		}
	}
	else if ((op & 0b11110) == 0b11100) {
		a6_7_106_t2(registers, first_halfword, second_halfword); // RSB
	}
}

static void a5_3_3(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op = (first_halfword & 0x01F0) >> 4;
	uint8_t rn = (first_halfword & 0x000F) >> 0;

	if (op == 0b00000) {
		if (!(rn == 0b1111)) {
			printf("  ADD a5_3_3\n");
			assert(false);
		}
		else {
			printf("  ADR a5_3_3\n");
			assert(false);
		}
	}
	else if (op == 0b00100) {
		a6_7_75_t3(registers, first_halfword, second_halfword); // MOV
	}
	else if (op == 0b01010) {
		if (!(rn == 0b1111)) {
			printf("  SUB a5_3_3\n");
			assert(false);
		}
		else {
			printf("  ADR a5_3_3\n");
			assert(false);
		}
	}
	else if (op == 0b01100) {
		a6_7_78_t1(registers, first_halfword, second_halfword); // MOVT
	}
	else if ((op & 0b11101) == 0b10000) {
		printf("  SSAT a5_3_3\n");
		assert(false);
	}
	else if (op == 0b10100) {
		assert(false);
	}
	else if (op == 0b10110) {
		assert(false);
	}
	else if ((op & 0b11101) == 0b11000) {
		printf("  USAT a5_3_3\n");
		assert(false);
	}
	else if (op == 0b11100) {
		a6_7_144_t1(registers,
		            first_halfword, second_halfword); // UBFX
	}
	else {
		assert(false);
	}
}

static void a5_3_4(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op1 = (first_halfword & 0x07F0) >> 4;
	uint8_t op2 = (second_halfword & 0x7000) >> 12;

	if (((op2 & 0b101) == 0b000) && !((op1 & 0b0111000) == 0b0111000)) {
		a6_7_12_t3(registers, first_halfword, second_halfword); // B
	}
	else if (((op2 & 0b101) == 0b000)
	         && ((op1 & 0b1111110) == 0b0111000)) {
		printf("MSR a5_3_4\n");
		assert(false);
	}
	else if (((op2 & 0b101) == 0b000) && (op1 == 0b0111010)) {
		printf("Hint a5_3_4\n");
		assert(false);
	}
	else if (((op2 & 0b101) == 0b000) && (op1 == 0b0111011)) {
		printf("Misc a5_3_4\n");
		assert(false);
	}
	else if (((op2 & 0b101) == 0b000)
	         && ((op1 & 0b1111110) == 0b0111110)) {
		printf("MSR a5_3_4\n");
		assert(false);
	}
	else if ((op2  == 0b010) && (op1 == 0b1111111)) {
		assert(false);
	}
	else if ((op2 & 0b101) == 0b001) {
		a6_7_12_t4(registers, first_halfword, second_halfword); // B
	}
	else if ((op2 & 0b101) == 0b101) {
		a6_7_18_t1(registers, first_halfword, second_halfword); // BL
	}
	else {
		assert(false);
	}
}

static void a5_3_5(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op = (first_halfword & 0x0180) >> 7;
	uint8_t W = (first_halfword & 0x0020) >> 5;
	uint8_t L = (first_halfword & 0x0010) >> 4;
	uint8_t Rn = (first_halfword & 0x000F) >> 0;

	if (op == 0b01) {
		if (L == 0) {
			printf("TODO 1: a5_3_5\n");
			assert(false);
		}
		else if (L == 1) {
			if (!((W == 1) && (Rn == 0b1101))) {
				printf("TODO 2: a5_3_5\n");
				assert(false);
			}
			else {
				a6_7_97_t2(registers, first_halfword,
				           second_halfword); // POP
			}
		}
	}
	else if (op == 0b10) {
		if (L == 0) {
			if (!(W == 1 && Rn == 0b1101)) {
				printf("TODO 3: a5_3_5\n");
				assert(false);
			}
			else {
				a6_7_98_t2(registers, first_halfword,
				           second_halfword); // PUSH
			}
		}
		else if (L == 1) {
			printf("TODO 3: a5_3_5\n");
			assert(false);
		}
	}
	else {
		assert(false);
	}
}

static void a5_3_7(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op1 = (first_halfword & 0x0180) >> 7;
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t op2 = (second_halfword & 0x0FC0) >> 6;

	if (n != 0b1111) {
		if (op1 == 0b01) {
			a6_7_42_t3(registers,
			           first_halfword,
			           second_halfword); // LDR
		}
		else {
			assert(op1 == 0b00);
			if ((op2 & 0x24) == 0x24) {
				a6_7_42_t4(registers,
				           first_halfword,
				           second_halfword); // LDR
			}
			else if ((op2 & 0x3C) == 0x30) {
				printf("LDR a5_3_7 2\n");
				assert(false);
			}
			else if ((op2 & 0x3C) == 0x38) {
				printf("LDRT a5_3_7\n");
				assert(false);
			}
			else if (op2 == 0b000000) {
				// LDR (register)
				a6_7_44_t2(registers,
				           first_halfword, second_halfword);
			}
			else {
				assert(false);
			}
		}
	}
	else {
		assert((op1 & 0b10) == 0b00);
		printf("  Load register a5_3_7\n");
		assert(false);
	}
}

static void a5_3_9(struct registers *registers,
                   uint16_t first_halfword,
                   uint16_t second_halfword)
{
	uint8_t op1 =  (first_halfword & 0x0180) >>  7;
	uint8_t n   =  (first_halfword & 0x000F) >>  7;
	uint8_t t   = (second_halfword & 0xF000) >> 12;
	uint8_t op2 = (second_halfword & 0x0FC0) >>  6;

	bool nNot1111 = n != 0b1111;
	bool tNot1111 = t != 0b1111;

	if ((op1 == 0b01) && nNot1111 && tNot1111) {
		// LDRB (immediate)
		a6_7_45_t2(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b00) && ((op2 & 0b100100) == 0b100100)
	         && nNot1111) {
		assert(false);
	}
	else if ((op1 == 0b00) && ((op2 & 0b111100) == 0b110000)
	         && nNot1111 && tNot1111) {
		assert(false);
	}
	else {
		printf("a5_3_9\n");
		assert(false);
	}
}

static void a5_3_10(struct registers *registers,
                    uint16_t first_halfword,
                    uint16_t second_halfword)
{
	uint8_t op1 =  (first_halfword & 0x00E0) >> 5;
	uint8_t op2 = (second_halfword & 0x0FC0) >> 6;

	if (op1 == 0b100) {
		// STRB (immediate)
		a6_7_121_t2(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b000) && ((op2 & 0b100000) == 0b100000)) {
		// STRB (immediate)
		a6_7_121_t3(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b000) && ((op2 & 0b100000) == 0b000000)) {
		assert(false); // STRB (register)
	}
	else if (op1 == 0b101) {
		assert(false); // STRH (immediate)
	}
	else if ((op1 == 0b001) && ((op2 & 0b100000) == 0b100000)) {
		assert(false); // STRH (immediate)
	}
	else if ((op1 == 0b001) && ((op2 & 0b100000) == 0b000000)) {
		assert(false); // STRH (register)
	}
	else if (op1 == 0b110) {
		// STR (immediate)
		a6_7_119_t3(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b010) && ((op2 & 0b100000) == 0b100000)) {
		a6_7_119_t4(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b010) && ((op2 & 0b100000) == 0b000000)) {
		// STR (register)
		a6_7_120_t2(registers, first_halfword, second_halfword);
	}
	else {
		assert(false);
	}
}

static void a5_3_11(struct registers *registers,
                    uint16_t first_halfword,
                    uint16_t second_halfword)
{
	uint8_t op =  (first_halfword & 0x01E0) >> 5;
	uint8_t S  =  (first_halfword & 0x0010) >> 4;
	uint8_t n  =  (first_halfword & 0x000F) >> 0;
	uint8_t d  = (second_halfword & 0x0F00) >> 8;

	if (op == 0b0000) {
		assert(false);
	}
	else if (op == 0b0001) {
		assert(false);
	}
	else if (op == 0b0010) {
		if (n != 0b1111) {
			// ORR (register)
			a6_7_91_t2(registers, first_halfword, second_halfword);
		}
		else {
			uint8_t imm3 = (second_halfword & 0x7000) >> 12;
			uint8_t imm2 = (second_halfword & 0x00C0) >>  6;
			uint8_t type = (second_halfword & 0x0030) >>  4;

			uint8_t imm5 = (imm3 << 2) | imm2;

			switch (type) {
			case 0b00:
				assert(false);
				break;
			case 0b01:
				a6_7_69_t2(registers,
				           first_halfword,
				           second_halfword); // LSR
				break;
			case 0b10:
				a6_7_10_t2(registers,
				           first_halfword,
				           second_halfword); // ASR
				break;
			case 0b11:
				assert(false);
				break;
			}
		}
	}
	else if (op == 0b0011) {
		assert(false);
	}
	else if (op == 0b0100) {
		assert(false);
	}
	else if (op == 0b1000) {
		if (d != 0b1111) {
			a6_7_4_t3(registers, first_halfword,
			          second_halfword); // ADD
		}
		else if (d == 0b1111) {
			if (S == 0) {
				assert(false);
			}
			else if (S == 1) {
				assert(false);
			}
		}
	}
	else if (op == 0b1010) {
		assert(false);
	}
	else if (op == 0b1011) {
		assert(false);
	}
	else if (op == 0b1101) {
		assert(false);
	}
	else if (op == 0b1110) {
		a6_7_107_t1(registers, first_halfword, second_halfword); // RSB
	}
}

static void a5_3_12(struct registers *registers,
                    uint16_t first_halfword,
                    uint16_t second_halfword)
{
	uint8_t op1 =  (first_halfword & 0x00F0) >> 4;
	uint8_t op2 = (second_halfword & 0x00F0) >> 4;

	if (op2 == 0b0000) {
		if ((op1 == 0b1110) == 0b0000) {
			a6_7_68_t2(registers, first_halfword,
			           second_halfword); // LSL
		}
		else if ((op1 == 0b1110) == 0b0010) {
			assert(false);
		}
		else if ((op1 == 0b1110) == 0b0100) {
			assert(false);
		}
		else if ((op1 == 0b1110) == 0b0110) {
			assert(false);
		}
		else {
			assert(false);
		}
	}
	else {
		assert(false);
	}
}

static void a5_3_14(struct registers *registers,
                    uint16_t first_halfword,
                    uint16_t second_halfword)
{
	uint8_t op1 = (first_halfword & 0x0070) >> 4;
	uint8_t a = (second_halfword & 0xF000) >> 12;
	uint8_t op2 = (second_halfword & 0x0030) >> 4;

	assert(op1 == 0b000);

	if (op2 == 0b00) {
		if (a != 0b1111) {
			a6_7_73_t1(registers, first_halfword, second_halfword);
		}
		else if (a == 0b1111) {
			printf("  MUL a5_3_14\n");
			assert(false);
		}
	}
	else if (op2 == 0b01) {
		a6_7_74_t1(registers, first_halfword, second_halfword); //MLS
	}
	else {
		assert(false);
	}
}

static void a5_3_15(struct registers *registers,
                    uint16_t first_halfword,
                    uint16_t second_halfword)
{
	uint8_t op1 = (first_halfword & 0x0070) >> 4;
	uint8_t op2 = (second_halfword & 0x00F0) >> 4;

	switch (op1) {
	case 0b000:
		assert(op2 == 0b0000);
		printf("  SMULL a5_3_15\n");
		assert(false);
		break;
	case 0b001:
		assert(op2 == 0b1111);
		printf("  SDIV a5_3_15\n");
		assert(false);
		break;
	case 0b010:
		assert(op2 == 0b0000);
		printf("  UMULL a5_3_15\n");
		assert(false);
		break;
	case 0b011:
		assert(op2 == 0b1111);
		a6_7_145_t1(registers, first_halfword, second_halfword);
		break;
	case 0b100:
		assert(op2 == 0b0000);
		printf("  SMLAL a5_3_15\n");
		assert(false);
		break;
	case 0b110:
		assert(op2 == 0b0000);
		printf("  UMLAL a5_3_15\n");
		assert(false);
		break;
	default:
		assert(false);
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

	if (op1 == 0b01) {
		if ((op2 & 0b1100100) == 0b0000000) {
			a5_3_5(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1100100) == 0b0000100) {
			printf("Load/store dual or exclusive, table branch\n");
			assert(false);
		}
		else if ((op2 & 0b1100000) == 0b0100000) {
			// Data processing (shifted register)
			a5_3_11(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1000000) == 0b1000000) {
			printf("Coprocessor instructions\n");
			assert(false);
		}
	}
	else if (op1 == 0b10) {
		if (((op2 & 0b0100000) == 0b0000000)
		     && (op == 0)) {
			// Data processing (modified immediate)
			a5_3_1(registers, first_halfword, second_halfword);
		}
		else if (((op2 & 0b0100000) == 0b0100000)
		     && (op == 0)) {
			// Data processing (plain binary immediate)
			a5_3_3(registers, first_halfword, second_halfword);
		}
		else if ((op == 1)) {
			// Branches and miscellaneous control
			a5_3_4(registers, first_halfword, second_halfword);
		}
	}
	else if (op1 == 0b11) {
		if ((op2 & 0b1110001) == 0b0000000) {
			// Store single data item
			a5_3_10(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1100111) == 0b0000001) {
			// Load bytes, memory hints
			a5_3_9(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1100111) == 0b0000011) {
			printf("Load halfword, unallocated memory hints\n");
			assert(false);
		}
		else if ((op2 & 0b1100111) == 0b0000101) {
			a5_3_7(registers,
			       first_halfword, second_halfword); // Load word
		}
		else if ((op2 & 0b1110000) == 0b0100000) {
			a5_3_12(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1111000) == 0b0110000) {
			a5_3_14(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1111000) == 0b0111000) {
			a5_3_15(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1000000) == 0b1000000) {
			printf("Coprocessor\n");
			assert(false);
		}
	}
}

static void step(struct registers *registers)
{
	is_branch = false;
	is_it_inst = false;

	uint16_t halfword = memory_halfword_read(registers->r[15]);
	if (((halfword & 0xE000) == 0xE000)
	    && ((halfword & 0x1800) != 0x0000)) {
		uint16_t first_halfword = halfword;
		uint16_t second_halfword
			= memory_halfword_read(registers->r[15] + 2);
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

	if (InITBlock(registers) && !is_it_inst) {
		ITAdvance(registers);
	}
}

void teensy_3_2_emulate(uint8_t *data, uint32_t length) {
	flash = data;

	struct registers registers;

	uint32_t initial_sp  = word_at_address(0x00000000);
	uint32_t initial_pc  = word_at_address(0x00000004);
	uint32_t nmi_address = word_at_address(0x00000008);

	printf("Initial Stack Pointer:   %08X\n", initial_sp);
	printf("Initial Program Counter: %08X\n", initial_pc);
	printf("NMI Address:             %08X\n", nmi_address);

	registers.apsr = 0; // Acutally unknown value

	/* R15 (Program Counter):
	   EPSR (Execution Program Status Register): bit 24 is the Thumb bit */
	const uint8_t EPSR_T_BIT = 24;

	registers.itstate = 0;
	registers.r[13] = initial_sp;
	registers.r[14] = 0xFFFFFFFF;
	registers.r[15] = initial_pc & 0xFFFFFFFE;
	registers.epsr = 0x01000000;
	if ((initial_pc & 0x00000001) == 0x00000001) {
		set_bit(&registers.epsr, EPSR_T_BIT);
	}

	printf("\nExecution:\n");
	for (int i = 0; i < 4378; ++i) {
		step(&registers);
	}
}
