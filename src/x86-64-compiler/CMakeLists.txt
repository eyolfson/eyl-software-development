# Copyright 2015-2016 Jonathan Eyolfson
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

project(X86_64_COMPILER C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wextra)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(x86-64-compiler
	main.c
	x86_64.c
)
