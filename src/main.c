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

#include "i8hex_parser.h"
#include "teensy_3_2.h"

static uint8_t data[0x20008000];

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 1;
	}

	size_t data_size;
	if (i8hex_parse(argv[1], data, 0x10000, &data_size) == FAILURE) {
		return 2;
	}

	teensy_3_2_emulate(data, data_size);
	return 0;
}
