#ifndef I8HEX_PARSER_H
#define I8HEX_PARSER_H

#include <stddef.h>
#include <stdint.h>

enum i8hex_parse_result {
	FAILURE,
	SUCCESS,
};

enum i8hex_parse_result i8hex_parse(const char *path, uint8_t *data_ptr,
                                    size_t data_capacity, size_t *data_size);

#endif
