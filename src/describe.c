#include <stdint.h>

struct memory_region {
	uint32_t start;
	uint32_t end;
};

struct memory_region program_flash = {
	.start = 0x00000000,
	.end   = 0x07FFFFFF,
};

struct memory_region peripheral_bridge_0 = {
	.start = 0x40000000,
	.end   = 0x4007FFFF,
};

struct memory_region peripheral_bridge_1 = {
	.start = 0x40080000,
	.end   = 0x400FEFFF,
};

struct memory_region general_purpose_input_output = {
	.start = 0x400FF000,
	.end   = 0x400FFFFF,
};

int main()
{

	return 0;
}
