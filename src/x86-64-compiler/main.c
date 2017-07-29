/*
 * Copyright 2015-2016 Jonathan Eyolfson
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux_syscall.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

static
int elf_simple_executable(const uint8_t *input, size_t input_size,
                          uint8_t **output, size_t *output_size)
{
	const uint16_t elf_header_size = 64;
	const uint16_t program_header_size = 56;
	const uint16_t section_header_size = 64;
	/* 0x08048000 is default for x86? */
  /* 0x00400000 is default for x86_64? */
	const uint32_t address = 0x10000;

	size_t data_size = elf_header_size + program_header_size + input_size + 12;
	uint8_t *data = malloc(data_size);

	if (data == NULL)
		return 1;

	data[ 0] = 0x7f; /* magic 0 */
	data[ 1] = 0x45; /* magic 1: E */
	data[ 2] = 0x4c; /* magic 2: L */
	data[ 3] = 0x46; /* magic 3: F */
	data[ 4] = 0x02; /* 64-bit */
	data[ 5] = 0x01; /* little endian */
	data[ 6] = 0x01; /* elf version */
	data[ 7] = 0x03; /* linux */
	data[ 8] = 0x00;
	data[ 9] = 0x00;
	data[10] = 0x00;
	data[11] = 0x00;
	data[12] = 0x00;
	data[13] = 0x00;
	data[14] = 0x00;
	data[15] = 0x00;
	*((uint16_t *)(data + 16)) = 0x0002; /* type: executable */
	*((uint16_t *)(data + 18)) = 0x003e; /* machine: x86_64 */
	*((uint32_t *)(data + 20)) = 0x00000001; /* version */
	*((uint64_t *)(data + 24)) = address + elf_header_size
	                             + program_header_size;
	*((uint64_t *)(data + 32)) = elf_header_size;
	*((uint64_t *)(data + 40)) = 0x0000000000000000;
	*((uint32_t *)(data + 48)) = 0x00000000; /* flags */
	*((uint16_t *)(data + 52)) = elf_header_size;
	*((uint16_t *)(data + 54)) = program_header_size;
	*((uint16_t *)(data + 56)) = 0x0001; /* number of program headers */
	*((uint16_t *)(data + 58)) = section_header_size;
	*((uint16_t *)(data + 60)) = 0x0000;
	*((uint16_t *)(data + 62)) = 0x0000;

	/* program header */
	*((uint32_t *)(data + 64)) = 0x00000001; /* type: LOAD */
	*((uint32_t *)(data + 68)) = 0x00000005; /* flags */
	*((uint64_t *)(data + 72)) = 0x00000000; /* offset */
	*((uint64_t *)(data + 80)) = address;
	*((uint64_t *)(data + 88)) = address;
	*((uint64_t *)(data + 96)) = data_size;
	*((uint64_t *)(data + 104)) = data_size;
	*((uint64_t *)(data + 112)) = 0x00000100; /* align */

	memcpy(data + (elf_header_size + program_header_size),
	       input, input_size);

	memcpy(data + (elf_header_size + program_header_size + input_size),
	       "Hello world\n", 12);

	*output = data;
	*output_size = data_size;
	return 0;
}

int main(int argc, char **argv)
{
	printf("x86-64 Compiler 0.0.1-development\n");

	mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	int fd = open("hello-world", O_WRONLY | O_CREAT, mode);
	if (fd == -1)
		return 1;

	/* linux.exit_group(0) */
	unsigned char instructions[] = {
		0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00, /* mov $0x01, %rax */
		0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00, /* mov $0x01, %rdi */
		0x48, 0xc7, 0xc6, 0xa6, 0x00, 0x01, 0x00, /* mov $0x100a6, %rsi */
		0x48, 0xc7, 0xc2, 0x0c, 0x00, 0x00, 0x00, /* mov $0x0c, %rdx */
		0x0f, 0x05,                               /* syscall */

		0x48, 0xc7, 0xc0, 0xe7, 0x00, 0x00, 0x00, /* mov $0xe7,%rax */
		0x48, 0xc7, 0xc7, 0x00, 0x00, 0x00, 0x00, /* mov $0x00,%rdi */
		0x0f, 0x05,                               /* syscall */
	};

	uint8_t *data;
	size_t data_size;
	if (elf_simple_executable(instructions, ARRAY_SIZE(instructions),
	                          &data, &data_size) != 0) {
		close(fd);
		return 1;
	}

	int ret = 0;
	if (write(fd, data, data_size) != (ssize_t)data_size)
		ret = 1;
	close(fd);
	free(data);
	return ret;
}
