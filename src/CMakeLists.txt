# Copyright 2016 Jonathan Eyolfson
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.1.3)

project(EYL_SOFTWARE_DEVELOPMENT C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_cadSTANDARD_REQUIRED ON)
add_compile_options(-Wextra)

add_executable(i8hex-reader
	main.c
	i8hex_parser.c
	teensy_3_2.c
	get_address_name.c
)

add_executable(describe
	describe.c
)

add_executable(bin-to-hex
	bin_to_hex.c
)

add_executable(arithmetic-test
	arithmetic.c
)

add_executable(teensy-compile
	teensy_compile.c
)

add_subdirectory(x86-64-compiler)

add_executable(diff-execution
	diff_execution.c
)
