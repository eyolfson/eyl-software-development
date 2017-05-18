#include <stdio.h>
#include <stdint.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

uint8_t program[] = {
	0x00, 0x80, 0x00, 0x20, 0xBD, 0x01, 0x00, 0x00,
	0x81, 0x13, 0x00, 0x00, 0x81, 0x13, 0x00, 0x00,
};

void i8hex_write(int fd, uint8_t *data, size_t size)
{
	uint8_t *current = data;
	uint16_t address = 0x0000;

	size_t remaining = size;

	char buf[45];

	while (remaining >= 16) {
		uint8_t checksum = 0;
		checksum += 16;       // Length
		checksum += address; // Address
		checksum += 0;       // Record type
		for (int i = 0; i < 16; ++i) {
			checksum += current[i];
		}
		checksum = ~checksum + 1;

		snprintf(buf, ARRAY_SIZE(buf),
		         ":10%04X00%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X"
		         "%02X%02X%02X%02X%02X%02X%02X\n", address,
		         current[0], current[1], current[2], current[3],
		         current[4], current[5], current[6], current[7],
		         current[8], current[9], current[10], current[11],
		         current[12], current[13], current[14], current[15],
		         checksum);
		write(fd, buf, ARRAY_SIZE(buf) - 1);

		current += 16;
		remaining -= 16;
	}
}

int main(int argc, const char *const *argv)
{
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int fd = open("blink.hex", O_RDWR | O_CREAT, mode);
	if (fd == -1) {
		return 1;
	}

	i8hex_write(fd, program, ARRAY_SIZE(program));

	if (close(fd) == -1) {
		return 1;
	}
	return 0;
}
