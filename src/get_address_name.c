#include "get_address_name.h"

const char *get_address_name(uint32_t address)
{
	const char *name = "";

	if (address >= 0xE0000000 && address <= 0xE00FFFFF) {
		name = "PPB (Private Peripheral Bus)";
	}

	switch (address) {
	case 0x40020000:
		name = "FTFL_FSTAT";
		break;
	case 0x40020001:
		name = "FTFL_FCNFG";
		break;
	case 0x40020002:
		name = "FTFL_FSEC";
		break;
	case 0x40020003:
		name = "FTFL_FOPT";
		break;
	case 0x40020004:
		name = "FTFL_FCCOB3";
		break;
	case 0x40020005:
		name = "FTFL_FCCOB2";
		break;
	case 0x40020006:
		name = "FTFL_FCCOB1";
		break;
	case 0x40020007:
		name = "FTFL_FCCOB0";
		break;
	case 0x40020008:
		name = "FTFL_FCCOB7";
		break;
	case 0x40020009:
		name = "FTFL_FCCOB6";
		break;
	case 0x4002000A:
		name = "FTFL_FCCOB5";
		break;
	case 0x4002000B:
		name = "FTFL_FCCOB4";
		break;
	case 0x4002000C:
		name = "FTFL_FCCOBB";
		break;
	case 0x4002000D:
		name = "FTFL_FCCOBA";
		break;
	case 0x4002000E:
		name = "FTFL_FCCOB9";
		break;
	case 0x4002000F:
		name = "FTFL_FCCOB8";
		break;
	case 0x40020010:
		name = "FTFL_FPROT3";
		break;
	case 0x40020011:
		name = "FTFL_FPROT2";
		break;
	case 0x40020012:
		name = "FTFL_FPROT1";
		break;
	case 0x40020013:
		name = "FTFL_FPROT0";
		break;
	case 0x40020016:
		name = "FTFL_FEPROT";
		break;
	case 0x40020017:
		name = "FTFL_FDPROT";
		break;
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
	case 0x4003D014:
		name = "RTC_SR";
		break;
	case 0x4003D018:
		name = "RTC_LR";
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
	case 0x40065000:
		name = "OSC_CR";
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
	case 0x40074000:
		name = "VREF_TRM";
		break;
	case 0x40074001:
		name = "VREF_SC";
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
	case 0xE000E400:
		name = "NVIC_IPR0";
		break;
	case 0xE000E404:
		name = "NVIC_IPR1";
		break;
	case 0xE000E5EC:
		name = "NVIC_IPR123";
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
