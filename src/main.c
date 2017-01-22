/*
 * Copyright 2016-2017 Jonathan Eyolfson
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "teensy_3_2.h"

const uint8_t START_CODE = ':';
static const uint8_t RECORD_BYTE_COUNT_MAX = 16;
static const uint8_t RECORD_TYPE_DATA = 0;
static const uint8_t RECORD_TYPE_END_OF_FILE = 1;

struct record {
	uint8_t byte_count;
	uint16_t address;
	uint8_t type;
	uint8_t data[16];
	uint8_t checksum;
};

static uint8_t data[0x37F4];

static uint8_t ascii_hex_to_value(uint8_t c)
{
	if (c < 'A') {
		return c - '0';
	}
	else {
		return 10 + (c - 'A');
	}
}

static int parse(uint8_t *state, struct record *record, bool *valid, uint8_t c)
{
	*valid = false;

	if (c == START_CODE) {
		record->byte_count = 0;
		record->address = 0;
		record->type = 0;
		memset(record->data, 0, 16);
		record->checksum = 0;
		*state = 1;
		return 0;
	}

	if (*state == 0) {
		return 0;
	}

	if (c < '0') {
		return 1;
	}
	else if (c > '9' && c < 'A') {
		return 1;
	}
	else if (c > 'F') {
		return 1;
	}

	uint8_t value = ascii_hex_to_value(c);

	switch (*state) {
	case 1:
		/* Byte count (16) */
		record->byte_count = value * 16;
		break;
	case 2:
		/* Byte count (1) */
		record->byte_count += value;
		if (record->byte_count > 16) {
			return 1;
		}
		break;
	case 3:
		/* Address (4096) */
		record->address = value * 4096;
		break;
	case 4:
		/* Address (256) */
		record->address += value * 256;
		break;
	case 5:
		/* Address (16) */
		record->address += value * 16;
		break;
	case 6:
		/* Address (1) */
		record->address += value;
		break;
	case 7:
		/* Record Type (16) */
		if (value != 0) {
			return 1;
		}
		break;
	case 8:
		/* Record Type (1) */
		if (value != RECORD_TYPE_DATA
		    && value != RECORD_TYPE_END_OF_FILE) {
			return 1;
		}
		record->type = value;
		break;
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
	case 21:
	case 22:
	case 23:
	case 24:
	case 25:
	case 26:
	case 27:
	case 28:
	case 29:
	case 30:
	case 31:
	case 32:
	case 33:
	case 34:
	case 35:
	case 36:
	case 37:
	case 38:
	case 39:
	case 40: {
		uint8_t i = (*state - 9) / 2;
		if (i == record->byte_count) {
			*state = 41;
			record->checksum = value * 16;
			break;
		}

		if ((*state % 2) == 1) {
			record->data[i] = value * 16;
		}
		else {
			record->data[i] += value;
		}
		break;
	}
	case 41:
		/* Checksum (16) */
		record->checksum = value * 16;
		break;
	case 42:
		/* Checksum (1) */
		record->checksum += value;
		break;
	}

	/* Advance to the next state */
	++(*state);

	if (*state == 43) {
		*valid = true;
		*state = 0;
	}

	return 0;
}

static uint16_t address_valid = 0x00;

static void record_valid(struct record *record) {
	if (record->type != RECORD_TYPE_DATA) {
		return;
	}

	assert(record->address == address_valid);
	for (uint16_t i = 0; i < record->byte_count; ++i) {
		uint16_t data_i = record->address + i;
		assert(data_i < 0x37F4);
		data[data_i] = record->data[i];
	}
	address_valid += record->byte_count;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 1;
	}

	const uint16_t BUF_LENGTH = 4096;
	uint8_t buf[BUF_LENGTH];

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		return 1;
	}

	int ret = 0;
	uint8_t state = 0;
	struct record record;
	bool valid;

	ssize_t bytes_read;
	bytes_read = read(fd, buf, BUF_LENGTH);
	while (bytes_read > 0) {
		uint16_t i = 0;
		while (i != bytes_read) {
			if (parse(&state, &record, &valid, buf[i]) != 0) {
				ret = 1;
				break;
			}
			if (valid) {
				record_valid(&record);
			}
			++i;
		}
		if (ret != 0) {
			break;
		}
		bytes_read = read(fd, buf, BUF_LENGTH);
	}

	if (address_valid == 0x37F4) {
		teensy_3_2_emulate(data, address_valid);
	}
	else {
		ret = 2;
	}

	if (bytes_read < 0) {
		ret = 1;
	}

	close(fd);
	return ret;
}
