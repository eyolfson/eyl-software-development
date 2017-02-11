#include "teensy_3_2.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/* SRAM_L = [0x1FFF8000, 0x20000000)
 * SRAM_U = [0x20000000, 0x20007FFF)
 */

#define SRAM_LOWER 0x1FFF8000
#define SRAM_UPPER 0x20007FFF

struct registers {
	uint32_t r[16];
	uint32_t apsr;
	uint32_t ipsr;
	uint32_t epsr;
	uint32_t primask;
	uint32_t faultmask;
	uint8_t itstate;
};

struct ThumbExpandImm_C_Result {
	uint32_t imm32;
	bool carry;
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

uint8_t CurrentCond(struct registers *registers)
{
	return (registers->itstate & 0xF0) >> 4;
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

struct AddWithCarry_Result AddWithCarry(uint32_t x, uint32_t y, bool carry_in)
{
	struct AddWithCarry_Result R;


	uint64_t unsigned_sum = ((uint64_t) x) + ((uint64_t) y);
	if (carry_in) unsigned_sum += 1;

	int64_t signed_sum = ((int32_t) x) + ((int32_t) y);
	if (carry_in) signed_sum += 1;

	R.result = (unsigned_sum & 0xFFFFFFFF);

	if (((uint64_t) R.result) == unsigned_sum)
		R.carry_out = false;
	else
		R.carry_out = true;

	if (((int64_t) ((int32_t) R.result)) == signed_sum)
		R.overflow = false;
	else
		R.overflow = true;

	return R;
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
}

bool InITBlock(struct registers *registers)
{
	return (registers->itstate & 0b00001111) != 0b0000;
}

bool LastInITBlock(struct registers *registers)
{
	return (registers->itstate & 0b00001111) == 0b1000;
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

struct ThumbExpandImm_C_Result ThumbExpandImm_C(uint16_t imm12, bool carry) {
	struct ThumbExpandImm_C_Result result;

	if ((imm12 & 0xC00) == 0x000) {
		uint8_t switch_value = (imm12 & 0x300) >> 8;
		switch (switch_value) {
		case 0b00:
			result.imm32 = (imm12 & 0x0FF);
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
		result.carry = carry;
	}
	else {
		uint8_t rotate = (imm12 & 0xF80) >> 7;
		uint32_t unrotated_value = 0x80 + (imm12 & 0x7F);
		uint32_t value = unrotated_value << (32 - rotate);
		result.imm32 = value;
		result.carry = false;
	}

	return result;
}

uint32_t ThumbExpandImm(struct registers *registers,
                        uint16_t imm12)
{
	return ThumbExpandImm_C(imm12, APSR_C(registers)).imm32;
}

static uint8_t *flash;

static bool is_branch;

static const char *get_condition_field(uint8_t cond)
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

static const char *get_address_name(uint32_t address)
{
	const char *name = NULL;

	if (address >= 0xE0000000 && address <= 0xE00FFFFF) {
		name = "PPB (Private Peripheral Bus)";
	}

	switch (address) {
	case 0x40038000:
		name = "FTM0_SC";
		break;
	case 0x40038004:
		name = "FTM0_CNT";
		break;
	case 0x40038008:
		name = "FTM0_MOD";
		break;
	case 0x4003800C:
		name = "FTM0_C0SC";
		break;
	case 0x40038010:
		name = "FTM0_C0V";
		break;
	case 0x40038014:
		name = "FTM0_C1SC";
		break;
	case 0x40038018:
		name = "FTM0_C1V";
		break;
	case 0x4003801C:
		name = "FTM0_C2SC";
		break;
	case 0x40038020:
		name = "FTM0_C2V";
		break;
	case 0x40038024:
		name = "FTM0_C3SC";
		break;
	case 0x40038028:
		name = "FTM0_C3V";
		break;
	case 0x4003802C:
		name = "FTM0_C4SC";
		break;
	case 0x40038030:
		name = "FTM0_C4V";
		break;
	case 0x40038034:
		name = "FTM0_C5SC";
		break;
	case 0x40038038:
		name = "FTM0_C5V";
		break;
	case 0x4003803C:
		name = "FTM0_C6SC";
		break;
	case 0x40038040:
		name = "FTM0_C6V";
		break;
	case 0x40038044:
		name = "FTM0_C7SC";
		break;
	case 0x40038048:
		name = "FTM0_C7V";
		break;
	case 0x4003804C:
		name = "FTM0_CTNIN";
		break;
	case 0x40038050:
		name = "FTM0_STATUS";
		break;
	case 0x40038054:
		name = "FTM0_MODE";
		break;
	case 0x40039000:
		name = "FTM1_SC";
		break;
	case 0x40039004:
		name = "FTM1_CNT";
		break;
	case 0x40039008:
		name = "FTM1_MOD";
		break;
	case 0x4003900C:
		name = "FTM1_C0SC";
		break;
	case 0x40039010:
		name = "FTM1_C0V";
		break;
	case 0x40039014:
		name = "FTM1_C1SC";
		break;
	case 0x40039018:
		name = "FTM1_C1V";
		break;
	case 0x4003901C:
		name = "FTM1_C2SC";
		break;
	case 0x40039020:
		name = "FTM1_C2V";
		break;
	case 0x40039024:
		name = "FTM1_C3SC";
		break;
	case 0x40039028:
		name = "FTM1_C3V";
		break;
	case 0x4003902C:
		name = "FTM1_C4SC";
		break;
	case 0x40039030:
		name = "FTM1_C4V";
		break;
	case 0x40039034:
		name = "FTM1_C5SC";
		break;
	case 0x40039038:
		name = "FTM1_C5V";
		break;
	case 0x4003903C:
		name = "FTM1_C6SC";
		break;
	case 0x40039040:
		name = "FTM1_C6V";
		break;
	case 0x40039044:
		name = "FTM1_C7SC";
		break;
	case 0x40039048:
		name = "FTM1_C7V";
		break;
	case 0x4003904C:
		name = "FTM1_CTNIN";
		break;
	case 0x40039050:
		name = "FTM1_STATUS";
		break;
	case 0x40039054:
		name = "FTM1_MODE";
		break;
	case 0x4003B000:
		name = "ADC0_SC1A";
		break;
	case 0x4003B004:
		name = "ADC0_SC1B";
		break;
	case 0x4003B008:
		name = "ADC0_CFG1";
		break;
	case 0x4003B00C:
		name = "ADC0_CFG2";
		break;
	case 0x4003B010:
		name = "ADC0_RA";
		break;
	case 0x4003B014:
		name = "ADC0_RB";
		break;
	case 0x4003B018:
		name = "ADC0_CV1";
		break;
	case 0x4003B01C:
		name = "ADC0_CV2";
		break;
	case 0x4003B020:
		name = "ADC0_SC2";
		break;
	case 0x4003B024:
		name = "ADC0_SC3";
		break;
	case 0x4003B028:
		name = "ADC0_OFS";
		break;
	case 0x4003D010:
		name = "RTC_CR";
		break;
	case 0x40047000:
		name = "SIM_SOPT1";
		break;
	case 0x40047004:
		name = "SIM_SOPT1CFG";
		break;
	case 0x40048004:
		name = "SIM_SOPT2";
		break;
	case 0x4004800C:
		name = "SIM_SOPT4";
		break;
	case 0x40048010:
		name = "SIM_SOPT5";
		break;
	case 0x40048018:
		name = "SIM_SOPT7";
		break;
	case 0x40048024:
		name = "SIM_SDID";
		break;
	case 0x40048028:
		name = "SIM_SCGC1";
		break;
	case 0x4004802C:
		name = "SIM_SCGC2";
		break;
	case 0x40048030:
		name = "SIM_SCGC3";
		break;
	case 0x40048034:
		name = "SIM_SCGC4";
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
	case 0x40048044:
		name = "SIM_CLKDIV1";
		break;
	case 0x40048048:
		name = "SIM_CLKDIV2";
		break;
	case 0x4004804C:
		name = "SIM_FCFG1";
		break;
	case 0x40048050:
		name = "SIM_FCFG2";
		break;
	case 0x40052000:
		name = "WDOG_STCTRLH";
		break;
	case 0x4005200E:
		name = "WDOG_UNLOCK";
		break;
	case 0x40064000:
		name = "MCG_C1";
		break;
	case 0x40064001:
		name = "MCG_C2";
		break;
	case 0x40064002:
		name = "MCG_C3";
		break;
	case 0x40064003:
		name = "MCG_C4";
		break;
	case 0x40064004:
		name = "MCG_C5";
		break;
	case 0x40064005:
		name = "MCG_C6";
		break;
	case 0x40064006:
		name = "MCG_S";
		break;
	case 0x40072000:
		name = "USB0_PERID";
		break;
	case 0x40072004:
		name = "USB0_IDCOMP";
		break;
	case 0x40072008:
		name = "USB0_REV";
		break;
	case 0x4007200C:
		name = "USB0_ADDINFO";
		break;
	case 0x40072010:
		name = "USB0_OTGISTAT";
		break;
	case 0x40072014:
		name = "USB0_OTGICR";
		break;
	case 0x40072018:
		name = "USB0_OTGSTAT";
		break;
	case 0x40072108:
		name = "USB0_CONTROL";
		break;
	case 0x4007210C:
		name = "USB0_USBTRC0";
		break;
	case 0x40072114:
		name = "USB0_USBFRMADJUST";
		break;
	case 0x4007D000:
		name = "PMC_LVDSC1";
		break;
	case 0x4007D001:
		name = "PMC_LVDSC2";
		break;
	case 0x4007D002:
		name = "PMC_REGSC";
		break;
	case 0x400B8000:
		name = "FTM2_SC";
		break;
	case 0x400B8004:
		name = "FTM2_CNT";
		break;
	case 0x400B8008:
		name = "FTM2_MOD";
		break;
	case 0x400B800C:
		name = "FTM2_C0SC";
		break;
	case 0x400B8010:
		name = "FTM2_C0V";
		break;
	case 0x400B8014:
		name = "FTM2_C1SC";
		break;
	case 0x400B8018:
		name = "FTM2_C1V";
		break;
	case 0x400B801C:
		name = "FTM2_C2SC";
		break;
	case 0x400B8020:
		name = "FTM2_C2V";
		break;
	case 0x400B8024:
		name = "FTM2_C3SC";
		break;
	case 0x400B8028:
		name = "FTM2_C3V";
		break;
	case 0x400B802C:
		name = "FTM2_C4SC";
		break;
	case 0x400B8030:
		name = "FTM2_C4V";
		break;
	case 0x400B8034:
		name = "FTM2_C5SC";
		break;
	case 0x400B8038:
		name = "FTM2_C5V";
		break;
	case 0x400B803C:
		name = "FTM2_C6SC";
		break;
	case 0x400B8040:
		name = "FTM2_C6V";
		break;
	case 0x400B8044:
		name = "FTM2_C7SC";
		break;
	case 0x400B8048:
		name = "FTM2_C7V";
		break;
	case 0x400B804C:
		name = "FTM2_CTNIN";
		break;
	case 0x400B8050:
		name = "FTM2_STATUS";
		break;
	case 0x400B8054:
		name = "FTM2_MODE";
		break;
	case 0x400BB000:
		name = "ADC1_SC1A";
		break;
	case 0x400BB004:
		name = "ADC1_SC1B";
		break;
	case 0x400BB008:
		name = "ADC1_CFG1";
		break;
	case 0x400BB00C:
		name = "ADC1_CFG2";
		break;
	case 0x400BB010:
		name = "ADC1_RA";
		break;
	case 0x400BB014:
		name = "ADC1_RB";
		break;
	case 0x400BB018:
		name = "ADC1_CV1";
		break;
	case 0x400BB01C:
		name = "ADC1_CV2";
		break;
	case 0x400BB020:
		name = "ADC1_SC2";
		break;
	case 0x400BB024:
		name = "ADC1_SC3";
		break;
	case 0x400BB028:
		name = "ADC1_OFS";
		break;
	case 0x400BB02C:
		name = "ADC1_PG";
		break;
	case 0x400BB030:
		name = "ADC1_MG";
		break;
	case 0xE000E010:
		name = "SYST_CSR";
		break;
	case 0xE000E014:
		name = "SYST_RVR";
		break;
	case 0xE000E018:
		name = "SYST_CVR";
		break;
	case 0xE000E100:
		name = "NVIC_ISER0";
		break;
	case 0xE000E104:
		name = "NVIC_ISER1";
		break;
	case 0xE000E108:
		name = "NVIC_ISER2";
		break;
	case 0xE004E004:
		name = "ICTR";
		break;
	case 0xE000ED00:
		name = "CPUID";
		break;
	case 0xE000ED04:
		name = "ICSR";
		break;
	case 0xE000ED08:
		name = "VTOR";
		break;
	}

	return name;
}

static void memory_write_byte(uint32_t address, uint8_t byte)
{
	const char *name = get_address_name(address);

	printf("  > MemU[%08X, 1]", address);
	if (name) {
		printf(" (%s)", name);
	}
	printf(" = %02X", byte);

	if (address < 0x20008000) {
		flash[address] = byte;
	}

	if (address >= 0xE0000000 && address <= 0xE000EFFF) {
		printf(" (System Control Space - SCS)");
	}

	printf("\n");
}

static void memory_write_halfword(uint32_t address, uint16_t halfword)
{
	const char *name = get_address_name(address);

	printf("  > MemU[%08X, 4]", address);
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

	printf("  > MemU[%08X, 4]", address);
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

static uint8_t memory_read_byte(uint32_t address)
{
	if (address < SRAM_LOWER) {
		uint8_t value = flash[address];
		printf("  > MemU[%08X, 1] = %02X\n", address, value);
		return value;
	}
	else if (address >= SRAM_LOWER && address <= SRAM_UPPER) {
		printf("  > MemU[%08X, 1] (SRAM) = %02X\n", address, 0);
		return 0;
	}
	else {
		const char *name = get_address_name(address);
		printf("  > MemU[%08X, 1]", address);
		if (name) {
			printf(" (%s)", name);
		}
		printf(" = 0 (DEFAULT)\n");
		return 0;
	}
}

static uint32_t memory_read_word(uint32_t address)
{
	if (address < SRAM_LOWER) {
		uint32_t value = word_at_address(address);
		printf("  > MemU[%08X, 4] = %08X\n", address, value);
		return value;
	}
	else if (address >= SRAM_LOWER && address <= SRAM_UPPER) {
		printf("  > MemU[%08X, 4] (SRAM) = %08X\n", address, 0);
		return 0;
	}
	else {
		const char *name = get_address_name(address);
		printf("  > UMem[%08X, 4]", address);
		if (name) {
			printf(" (%s)", name);
		}
		printf(" = 0 (DEFAULT)\n");
		return 0;
	}
}

static uint16_t halfword_at_address(uint32_t base)
{
	return flash[base] +
	       + (flash[base + 1] * 0x100);
}

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

static void a6_7_3_t2(struct registers *registers, uint16_t halfword)
{
	uint8_t d = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;
	uint8_t n = d;
	bool setflags = !InITBlock(registers);

	uint32_t imm32 = imm8;

	struct AddWithCarry_Result R =
		AddWithCarry(registers->r[n], imm32, false);

	printf("  ADD");
	if (setflags) {
		printf("S");
	}
	printf(" R%d #%d\n", d, imm32);

	registers->r[d] = R.result;
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_3_t3(struct registers *registers,
                      uint16_t first_halfword,
                      uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;

	bool setflags = S == 1;
	struct ThumbExpandImm_C_Result TR = ThumbExpandImm_C(imm12, false);

	printf("  ADD");
	if (setflags) {
		printf("S");
	}
	printf(".W R%d, R%d, #%d\n", d, n, TR.imm32);

	struct AddWithCarry_Result AR = AddWithCarry(registers->r[n], TR.imm32,
	                                             false);
	registers->r[d] = AR.result;
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_4_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;
	printf("  ADDS R%d, R%d, R%d\n", d, n, m);

	uint32_t result = registers->r[n] + registers->r[m];

	registers->r[d] = result;
	printf("  > R%d = %08X\n", d, result);

	/* TODO: Add with carry */
	/* TODO: Set flags */
}

static void a6_7_8_t1(struct registers *registers,
                      uint16_t first_halfword,
                      uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t rn = (first_halfword & 0x000F);
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t rd = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF);

	if (rd == 0xF && S == 1) {
		assert(false);
	}

	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;

	struct ThumbExpandImm_C_Result result = ThumbExpandImm_C(imm12, false);

	printf("  AND");
	if (S) {
		printf("S");
	}
	printf(" R%d, R%d, #0x%08X\n", rd, rn, result.imm32);

	registers->r[rd] = registers->r[rn] & result.imm32;
	printf("  > R%d = %08X\n", rd, registers->r[rd]);
}

static void a6_7_12_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t cond = (halfword & 0x0F00) >> 8;
	uint8_t imm8 = (halfword & 0x00FF);
	uint32_t imm32 = imm8 << 1;

	uint32_t address = registers->r[15] + 4 + imm32;
	printf("  B[todo] %08X\n", address);
}

static void a6_7_12_t2(struct registers *registers, uint16_t halfword)
{
	uint16_t imm11 = (halfword & 0x7FF);
	uint32_t imm32 = imm11 * 0x2;

	uint32_t address = PC(registers) + imm32;
	printf("  B.N label_%08X\n", address);
	registers->r[15] = address;
	is_branch = true;
	printf("  > R15 = %08X\n", address);
}

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

static void a6_7_20_t1(struct registers *registers,
                       uint16_t first_halfword)
{
	uint8_t m = (first_halfword & 0x0078) >> 3;
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

	if (registers->r[rn] == 0) {
		registers->r[15] = address;
		printf("  > R15 = %08X\n", address);
		is_branch = true;
	}
}

static void a6_7_27_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t n = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;

	// TODO: CMP
	printf("  CMP R%d, #%d\n", n, imm32);
}

static void a6_7_28_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t m = (halfword & 0x0038) >> 3;
	uint8_t n = (halfword & 0x0007) >> 0;
	enum SRType shift_t = SRType_LSL;
	uint8_t shift_n = 0;

	// TODO: CMP
	printf("  CMP R%d, R%d\n", n, m);
}

static void a6_7_37_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t firstcond = (halfword & 0x00F0) >> 4;
	uint8_t mask = (halfword & 0x000F) >> 0;

	printf("  IT");
	switch (firstcond) {
	case 0b0000:
		printf(" EQ");
		break;
	case 0b0001:
		printf(" NE");
		break;
	case 0b0010:
		printf(" CS");
		break;
	case 0b0011:
		printf(" CC");
		break;
	case 0b0100:
		printf(" MI");
		break;
	case 0b0101:
		printf(" PL");
		break;
	case 0b0110:
		printf(" VS");
		break;
	case 0b0111:
		printf(" VC");
		break;
	case 0b1000:
		printf(" HI");
		break;
	case 0b1001:
		printf(" LS");
		break;
	case 0b1010:
		printf(" GE");
		break;
	case 0b1011:
		printf(" LT");
		break;
	case 0b1100:
		printf(" GT");
		break;
	case 0b1101:
		printf(" LE");
		break;
	case 0b1110:
		break;
	case 0b1111:
		break;
	}
	printf("\n");

	assert(mask == 0b1000);

	registers->itstate = (halfword & 0x00FF) >> 0;
	printf("  > ITSTATE = %02X\n", registers->itstate);
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

static void a6_7_43_t1(struct registers *registers, uint16_t halfword)
{
	uint8_t t = (halfword & 0x0700) >> 8;
	uint8_t imm8 = halfword;

	uint32_t imm32 = imm8 * 0x4;

	uint32_t base = Align_PC_4(registers);
	uint32_t address = base + imm32;

	printf("  LDR R%d [PC, #%d]\n", t, imm32);
	uint32_t data = memory_read_word(address);
	registers->r[t] = data;
	printf("  > R%d = %08X\n", t, registers->r[t]);
}

static void a6_7_45_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t rn = (halfword & 0x0038) >> 3;
	uint8_t rt = (halfword & 0x0007);
	uint32_t imm32 = imm5;

	uint32_t offset_addr = registers->r[rn] + imm32;
	uint32_t address = offset_addr;

	printf("  LDRB R%d [R%d, #0x%08X]\n", rt, rn, imm32);
	uint32_t value = memory_read_byte(address);
	registers->r[rt] = value;
	printf("  > R%d = %08X\n", rt, value);
}

static void a6_7_67_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t m = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;

	assert(imm5 != 0b00000);
	printf("  LSL R%d, R%d, #%d\n", d, m, imm5);
	// TODO
	registers->r[d] = registers->r[m] << imm5;
	printf("  R%d = %08X\n", d, registers->r[d]);
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

static void a6_7_75_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t rd = (halfword & 0x0700) >> 8;
	uint8_t imm8 = (halfword & 0x00FF);
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

	struct ThumbExpandImm_C_Result result = ThumbExpandImm_C(imm12, false);
	registers->r[rd] = result.imm32;
	printf("  MOV.W R%d #0x%08X\n", rd, result.imm32);
	printf("  > R%d = %08X\n", rd, result.imm32);
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
	uint8_t D = (halfword & 0x0080) >> 7;
	uint8_t m = (halfword & 0x0078) >> 3;
	uint8_t Rd = (halfword & 0x0007) >> 0;

	uint8_t d = (D * 0x8) + Rd;
	bool setflags = false;

	uint32_t result = registers->r[m];
	assert(d != 15);


	printf("  MOV R%d, R%d\n", d, m);
	registers->r[d] = result;
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_87_t1(struct registers *registers, uint16_t halfword)
{
	printf("  NOP\n");
}

static void a6_7_97_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t register_list = (halfword & 0x00FF) >> 0;
	uint8_t P = (halfword & 0x0100) >> 8;

	uint16_t all_registers = (P << 15) | register_list;
	uint8_t bit_count = __builtin_popcount(all_registers);

	uint32_t address = SP(registers);

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
}

static void a6_7_98_t1(struct registers *registers,
                       uint16_t halfword)
{
	uint8_t register_list = (halfword & 0x00FF) >> 0;
	uint8_t M = (halfword & 0x0100) >> 8;
	uint16_t all_registers = (M << 14) | register_list;

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

static void a6_7_106_t2(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t i = (first_halfword & 0x0400) >> 10;
	uint8_t S = (first_halfword & 0x0010) >> 4;
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t imm3 = (second_halfword & 0x7000) >> 12;
	uint8_t d = (second_halfword & 0x0F00) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint16_t imm12 = (i * 0x800)
	                 + (imm3 * 0x100)
	                 + imm8;

	bool setflags = S == 1;
	uint32_t imm32 = ThumbExpandImm(registers, imm12);

	struct AddWithCarry_Result R =
		AddWithCarry(~(registers->r[n]), imm32, true);

	printf("  RSB");
	if (setflags) {
		printf("S");
	}
	printf(" R%d, R%d, #0x%08X\n", d, n, imm32);

	registers->r[d] = R.result;
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void STR_immediate(struct registers *registers,
                          uint8_t t, uint8_t n, int32_t imm32,
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
			printf("  STR R%d, [R%d, #%d]\n", t, n, imm32);

		}
		else {
			printf("  STR R%d, [R%d, #%d]!\n", t, n, imm32);
		}
	}
	else {
		printf("  STR R%d, [R%d], #%d\n", t, n, imm32);
	}
	memory_write_word(address, registers->r[t]);

	if (wback) {
		printf("  > R%d = %08X\n", n, offset_addr);
		registers->r[n] = offset_addr;
	}
}

static void a6_7_119_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007);

	uint32_t imm32 = imm5 << 2;
	bool index = true;
	bool add = true;
	bool wback = false;

	STR_immediate(registers, t, n, imm32, index, add, wback);
}

static void a6_7_119_t4(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t t = (second_halfword & 0xF000) >> 12;
	uint8_t P = (second_halfword & 0x0400) >> 10;
	uint8_t U = (second_halfword & 0x0200) >> 9;
	uint8_t W = (second_halfword & 0x0100) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;
	bool index = P == 1;
	bool add = U == 1;
	bool wback = W == 1;

	STR_immediate(registers, t, n, imm32, index, add, wback);
}

static void STRB(struct registers *registers,
                 uint8_t t, uint8_t n, int32_t imm32,
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
	memory_write_byte(address, value);

	if (wback) {
		registers->r[n] = offset_addr;
		printf("  > R%d = %08X\n", n, offset_addr);
	}
}

static void a6_7_121_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t imm5 = (halfword & 0x07C00) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t t = (halfword & 0x0007) >> 0;

	uint32_t imm32 = imm5;
	bool index = true;
	bool add = true;
	bool wback = false;

	STRB(registers, t, n, imm32, index, add, wback);
}

static void a6_7_121_t3(struct registers *registers,
                        uint16_t first_halfword,
                        uint16_t second_halfword)
{
	uint8_t n = (first_halfword & 0x000F) >> 0;
	uint8_t t = (second_halfword & 0xF000) >> 12;
	uint8_t P = (second_halfword & 0x0400) >> 10;
	uint8_t U = (second_halfword & 0x0200) >> 9;
	uint8_t W = (second_halfword & 0x0100) >> 8;
	uint8_t imm8 = (second_halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;
	bool index = P == 1;
	bool add = U == 1;
	bool wback = W == 1;

	STRB(registers, t, n, imm32, index, add, wback);
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

	memory_write_halfword(address, value);
}

static void a6_7_132_t2(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t d = (halfword & 0x0700) >> 8;
	uint8_t n = d;
	uint8_t imm8 = (halfword & 0x00FF) >> 0;

	uint32_t imm32 = imm8;

	printf("  SUB R%d, R%d, #%d\n", d, n, imm32);

	struct AddWithCarry_Result R =
		AddWithCarry(registers->r[n], ~imm32, true);

	// TODO: setflags
	registers->r[d] = R.result;
	printf("  > R%d = %08X\n", d, registers->r[d]);
}

static void a6_7_133_t1(struct registers *registers,
                        uint16_t halfword)
{
	uint8_t m = (halfword & 0x01C0) >> 6;
	uint8_t n = (halfword & 0x0038) >> 3;
	uint8_t d = (halfword & 0x0007) >> 0;

	printf("  SUB R%d, R%d, R%d\n", d, n, m);

	uint32_t shifted = registers->r[m];
	struct AddWithCarry_Result R =
		AddWithCarry(registers->r[n], ~shifted, true);

	// TODO: setflags
	registers->r[d] = R.result;
	printf("  > R%d = %08X\n", d, registers->r[d]);
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
	uint8_t rm = (halfword & 0x0031) >> 3;
	uint8_t rd = (halfword & 0x0007);

	uint32_t value = (registers->r[rm] & 0x000000FF);

	printf("  UXTB R%d, R%d\n", rd, rm);
	printf("  > R%d = %08X\n", rd, value);
	registers->r[rd] = value;
}

static void a5_2_1(struct registers *registers,
                   uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x3E00) >> 9;

	if ((opcode & 0b11100) == 0b00000) {
		a6_7_67_t1(registers, halfword); // LSL
	}
	else if ((opcode & 0b11100) == 0b00100) {
		printf("  LSR?\n");
	}
	else if ((opcode & 0b11100) == 0b01000) {
		printf("  ASR?\n");
	}
	else if (opcode == 0b01100) {
		a6_7_4_t1(registers, halfword);
	}
	else if (opcode == 0b01101) {
		a6_7_133_t1(registers, halfword); // SUB
	}
	else if (opcode == 0b01110) {
		printf("  ADD?\n");
	}
	else if (opcode == 0b01111) {
		printf("  SUB?\n");
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
		break;
	case 0b0001:
		printf("  EOR? a5_2_2\n");
		break;
	case 0b0010:
		printf("  LSL? a5_2_2\n");
		break;
	case 0b0011:
		printf("  LSR? a5_2_2\n");
		break;
	case 0b0100:
		printf("  ASR? a5_2_2\n");
		break;
	case 0b0101:
		printf("  ADC? a5_2_2\n");
		break;
	case 0b0110:
		printf("  SBC? a5_2_2\n");
		break;
	case 0b0111:
		printf("  ROR? a5_2_2\n");
		break;
	case 0b1000:
		printf("  TST? a5_2_2\n");
		break;
	case 0b1001:
		printf("  RSB? a5_2_2\n");
		break;
	case 0b1010:
		a6_7_28_t1(registers, halfword); // CMP
		break;
	case 0b1011:
		printf("  CMN? a5_2_2\n");
		break;
	case 0b1100:
		printf("  ORR? a5_2_2\n");
		break;
	case 0b1101:
		printf("  MUL? a5_2_2\n");
		break;
	case 0b1110:
		printf("  BIC? a5_2_2\n");
		break;
	case 0b1111:
		printf("  MVN? a5_2_2\n");
		break;
	}
}

static void a5_2_3(struct registers *registers,
                   uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x03C0) >> 6;
	if ((opcode & 0b1100) == 0b0000) {
		printf("  ADD? a5_2_3");
	}
	else if (opcode == 0b0100) {
		assert(false);
	}
	else if (opcode == 0b0101) {
		printf("  CMP? a5_2_3");
	}
	else if ((opcode & 0b1110) == 0b0110) {
		printf("  CMP? a5_2_3");
	}
	else if ((opcode & 0b1100) == 0b1000) {
		a6_7_76_t1(registers, halfword); // MOV
	}
	else if ((opcode & 0b1110) == 0b1100) {
		a6_7_20_t1(registers, halfword); // BX
	}
	else if ((opcode & 0b1110) == 0b1110) {
		printf("  BLX? a5_2_3");
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

	if (opA == 0b0110) {
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
	else if (opA == 0x8) {
		if ((opB & 0x4) == 0x0) {
			a6_7_128_t1(registers, halfword);
		}
	}
}

static void a5_2_5(struct registers *registers, uint16_t halfword)
{
	uint8_t opcode = (halfword & 0x0FE0) >> 5;
	if (opcode == 0b0110011) {
		b4_1_1_t1(registers, halfword); // CPS
	}
	else if ((opcode & 0b1111100) == 0b0000000) {
		printf("  ADD? a5_2_5\n");
		assert(false);
	}
	else if ((opcode & 0b1111100) == 0b0000100) {
		printf("  SUB? a5_2_5\n");
		assert(false);
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

	if ((opcode & 0b110000) == 0b000000) {
		a5_2_1(registers, halfword);
	}
	else if (opcode == 0x10) {
		a5_2_2(registers, halfword);
	}
	else if (opcode == 0x11) {
		a5_2_3(registers, halfword);
	}
	else if ((opcode & 0b111110) == 0b010010) {
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
	else if ((opcode & 0b111110) == 0b101000) {
		printf("  ADR? a5_2\n");
	}
	else if ((opcode & 0b111110) == 0b101010) {
		printf("  ADD? a5_2\n");
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
		}
	}
	else if ((op & 0b11110) == 0b00010) {
		printf("  BIC? a5_3_1\n");
	}
	else if ((op & 0b11110) == 0b00100) {
		if (!(rn == 0b1111)) {
			printf("  ORR? a5_3_1\n");
		}
		else if (rn == 0b1111) {
			a6_7_75_t2(registers, first_halfword, second_halfword);
		}
	}
	else if ((op & 0b11110) == 0b00110) {
		if (!(rn == 0b1111)) {
			printf("  ORN? a5_3_1\n");
		}
		else if (rn == 0b1111) {
			printf("  MVN? a5_3_1\n");
		}
	}
	else if ((op & 0b11110) == 0b01000) {
		if (!(rd == 0b1111)) {
			printf("  EOR? a5_3_1\n");
		}
		else if (rd == 0b1111) {
			printf("  TEQ? a5_3_1\n");
		}
	}
	else if ((op & 0b11110) == 0b10000) {
		if (!(rd == 0b1111)) {
			a6_7_3_t3(registers, first_halfword,
			          second_halfword); // ADD
		}
		else if (rd == 0b1111) {
			printf("  CMN? a5_3_1\n");
		}
	}
	else if ((op & 0b11110) == 0b10100) {
		printf("  ADC? a5_3_1\n");
	}
	else if ((op & 0b11110) == 0b10110) {
		printf("  SBC? a5_3_1\n");
	}
	else if ((op & 0b11110) == 0b11010) {
		if (!(rd == 0b1111)) {
			printf("  SUB? a5_3_1\n");
		}
		else if (rd == 0b1111) {
			printf("  CMP? a5_3_1\n");
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
	uint8_t op = ((first_halfword & 0x01F0) >> 4);
	if (op == 0x04) {
		a6_7_75_t3(registers, first_halfword, second_halfword);
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

static void a5_3_10(struct registers *registers,
                    uint16_t first_halfword,
                    uint16_t second_halfword)
{
	uint8_t op1 = (first_halfword & 0x00E0) >> 5;
	uint8_t op2 = (second_halfword & 0x0FC0) >> 6;

	if (op1 == 0b100) {
	}
	else if ((op1 == 0b000) && ((op2 & 0b100000) == 0b100000)) {
		a6_7_121_t3(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b010) && ((op2 & 0b100000) == 0b100000)) {
		a6_7_119_t4(registers, first_halfword, second_halfword);
	}
	else if ((op1 == 0b010) && ((op2 & 0b100000) == 0b000000)) {
		printf("STR a5_3_10\n");
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
		}
	}
	else if (op2 == 0b01) {
		printf("  MLS a5_3_14\n");
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
		break;
	case 0b001:
		assert(op2 == 0b1111);
		printf("  SDIV a5_3_15\n");
		break;
	case 0b010:
		assert(op2 == 0b0000);
		printf("  UMULL a5_3_15\n");
		break;
	case 0b011:
		assert(op2 == 0b1111);
		a6_7_145_t1(registers, first_halfword, second_halfword);
		break;
	case 0b100:
		assert(op2 == 0b0000);
		printf("  SMLAL a5_3_15\n");
		break;
	case 0b110:
		assert(op2 == 0b0000);
		printf("  UMLAL a5_3_15\n");
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
	}
	else if (op1 == 0b10) {
		if (((op2 & 0b0100000) == 0b0000000)
		     && (op == 0)) {
			a5_3_1(registers, first_halfword, second_halfword);
		}
		else if (((op2 & 0b0100000) == 0b0100000)
		     && (op == 0)) {
			a5_3_3(registers, first_halfword, second_halfword);
		}
		else if ((op == 1)) {
			a5_3_4(registers, first_halfword, second_halfword);
		}
	}
	else if (op1 == 0b11) {
		if ((op2 & 0b1110001) == 0b0000000) {
			a5_3_10(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1100111) == 0b0000001) {
			printf("Load bytes, memory hints\n");
		}
		else if ((op2 & 0b1100111) == 0b0000011) {
			printf("Load halfword, unallocated memory hints\n");
		}
		else if ((op2 & 0b1100111) == 0b0000101) {
			printf("Load word\n");
		}
		else if ((op2 & 0b1110000) == 0b0100000) {
			printf("Data processing\n");
		}
		else if ((op2 & 0b1111000) == 0b0110000) {
			a5_3_14(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1111000) == 0b0111000) {
			a5_3_15(registers, first_halfword, second_halfword);
		}
		else if ((op2 & 0b1000000) == 0b1000000) {
			printf("Coprocessor\n");
		}
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
	for (int i = 0; i < 287; ++i){
		step(&registers);
	}
}
