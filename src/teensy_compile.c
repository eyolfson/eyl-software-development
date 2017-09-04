#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#define ARRAY_LENGTH(a) (sizeof((a))/sizeof((a)[0]))

static uint32_t nvic[111];

int main(int argc, const char *argv[])
{
	int fd = open("teensy.bin",
	              O_CREAT | O_WRONLY,
	              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (fd == -1)
		return 1;

	nvic[0] = 0x20008000;
	nvic[1] = 0x000001BF;
	for (size_t i = 2; i < 111; ++i) {
		nvic[i] = 0x000001BD;
	}

	uint8_t instructions[4];
	instructions[0] = 0xFE;
	instructions[1] = 0xE7;
	instructions[2] = 0xFE;
	instructions[3] = 0xE7;

	if (write(fd, nvic, sizeof(nvic)) != sizeof(nvic))
		return 1;

	if (write(fd, instructions, sizeof(instructions))
	    != sizeof(instructions))
		return 1;

	close(fd);

	return 0;
}
