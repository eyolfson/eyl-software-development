#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof((a))/sizeof((a)[0]))

static uint32_t nvic[111];

enum inst_kind {
	BRANCH,
	LOAD,
	STORE_2_BYTE,
	STORE_4_BYTE,
};

struct inst {
	enum inst_kind kind;
	uint8_t *pos;
};

struct branch_inst {
	struct inst inst;
	struct inst *dst;
};

struct load_inst {
	struct inst inst;
	uint8_t reg;
	uint32_t value;
};

struct store_2_byte_inst {
	struct inst inst;
	uint8_t reg_addr;
	uint8_t reg_value;
};

struct store_4_byte_inst {
	struct inst inst;
	uint8_t reg_addr;
	uint8_t reg_value;
};

struct insts {
	uint8_t data[4096];
	size_t size;
	size_t capacity;
};

struct context {
	uint32_t start_addr;
	uint8_t buf[256];
	uint8_t *pos;
	uint8_t *end;
	struct load_inst *literal_pool[16];
	uint8_t literal_pool_size;
	uint8_t *insts_pos;
};

void generate_branch_inst(struct context *c, struct branch_inst *branch_inst)
{
	branch_inst->inst.pos = c->pos;

	assert((struct inst *) branch_inst == branch_inst->dst);
	*c->pos = 0xFE;
	++c->pos;
	*c->pos = 0xE7;
	++c->pos;
}

void generate_load_inst(struct context *c, struct load_inst *load_inst)
{
	load_inst->inst.pos = c->pos;

	/* allocate space for the instruction, literal pool doesn't exist */
	c->pos += 2;

	/* add value to the literal pool */
	assert(ARRAY_SIZE(c->literal_pool) != c->literal_pool_size);
	c->literal_pool[c->literal_pool_size] = load_inst;
	++c->literal_pool_size;
}

void generate_store_2_byte_inst(struct context *c,
                                struct store_2_byte_inst *store_2_byte_inst)
{
	assert(store_2_byte_inst->reg_addr < 8);
	assert(store_2_byte_inst->reg_value < 8);

	*c->pos = (store_2_byte_inst->reg_addr << 3)
	          | (store_2_byte_inst->reg_value);
	++c->pos;
	*c->pos = 0x80;
	++c->pos;
}

void generate_store_4_byte_inst(struct context *c,
                                struct store_4_byte_inst *store_4_byte_inst)
{
	assert(store_4_byte_inst->reg_addr < 8);
	assert(store_4_byte_inst->reg_value < 8);

	*c->pos = (store_4_byte_inst->reg_addr << 3)
	          | (store_4_byte_inst->reg_value);
	++c->pos;
	*c->pos = 0x60;
	++c->pos;
}

void align_literal_pool(struct context *c)
{
	uint32_t literal_start_addr = c->start_addr + (c->pos - c->buf);
	assert((literal_start_addr % 2) == 0);
	/* Align to 4 bytes */
	if ((literal_start_addr % 4) != 0) {
		*c->pos = 0xFF;
		++c->pos;
		*c->pos = 0xFF;
		++c->pos;
	}
}

void generate_literal_pool(struct context *c)
{
	if (c->literal_pool_size == 0)
		return;

	align_literal_pool(c);

	for (size_t i = 0; i < c->literal_pool_size; ++i) {
		struct load_inst *load_inst = c->literal_pool[i];

		uint8_t *value_pos = c->pos;
		/* assume this platform is little endian */
		*((uint32_t *) value_pos) = load_inst->value;
		c->pos += sizeof(uint32_t);
		uint32_t value_addr = c->start_addr + (value_pos - c->buf);

		assert(load_inst->reg < 8);
		uint8_t *load_pos = load_inst->inst.pos;
		uint32_t first_addr = (c->start_addr + (load_pos - c->buf) + 4)
		                      & ~0x3;

		uint32_t index = (value_addr - first_addr) / 4;
		assert(index < 256);

		*load_pos = index;
		++load_pos;
		*load_pos = 0x48 + load_inst->reg;

	}
	c->literal_pool_size = 0;
}

void generate_inst(struct context *c, struct insts *insts)
{
	struct inst *inst = (struct inst *) c->insts_pos;
	size_t inst_size = 0;

	switch (inst->kind) {
	case BRANCH:
		generate_branch_inst(c, (struct branch_inst *) inst);
		inst_size = sizeof(struct branch_inst);
		break;
	case LOAD:
		generate_load_inst(c, (struct load_inst *) inst);
		inst_size = sizeof(struct load_inst);
		break;
	case STORE_2_BYTE:
		generate_store_2_byte_inst(c, (struct store_2_byte_inst *) inst);
		inst_size = sizeof(struct store_2_byte_inst);
		break;
	case STORE_4_BYTE:
		generate_store_4_byte_inst(c, (struct store_4_byte_inst *) inst);
		inst_size = sizeof(struct store_4_byte_inst);
		break;
	}

	assert(inst_size != 0);
	c->insts_pos += inst_size;
}

/*
void generate_insts(struct context *c, struct inst **insts, size_t insts_size)
{
	for (size_t i = 0; i < insts_size; ++i) {
		generate_inst(c, insts[i]);
	}

	generate_literal_pool(c);
}
*/

void generate_insts(struct context *c, struct insts *insts)
{
	assert(insts->size != 0);
	c->insts_pos = insts->data;

	while(c->insts_pos < (insts->data + insts->size)) {
		generate_inst(c, insts);
	}
/*
	for (size_t i = 0; i < insts_size; ++i) {
		generate_inst(c, insts[i]);
	}
*/

	generate_literal_pool(c);
}

void add_bytes(struct insts *insts, uint8_t *data, size_t size)
{
	size_t remaining = insts->capacity - insts->size;
	assert(remaining >= size);
	memcpy(insts->data + insts->size, data, size);
	insts->size += size;
}

void add_load_reg_val(struct insts *insts, uint8_t reg, uint32_t val)
{
	struct load_inst load = {
		.inst = {
			.kind = LOAD,
		},
		.reg = reg,
		.value = val,
	};
	add_bytes(insts, (uint8_t *) &load, sizeof(load));
}

void add_store_2_bytes_reg_reg(struct insts *insts, uint8_t addr, uint8_t val)
{
	struct store_2_byte_inst store = {
		.inst = {
			.kind = STORE_2_BYTE,
		},
		.reg_addr = addr,
		.reg_value = val,
	};
	add_bytes(insts, (uint8_t *) &store, sizeof(store));
}

void add_store_4_bytes_reg_reg(struct insts *insts, uint8_t addr, uint8_t val)
{
	struct store_4_byte_inst store = {
		.inst = {
			.kind = STORE_4_BYTE,
		},
		.reg_addr = addr,
		.reg_value = val,
	};
	add_bytes(insts, (uint8_t *) &store, sizeof(store));
}

void add_infinite_loop(struct insts *insts)
{
	struct branch_inst loop = {
		.inst = {
			.kind = BRANCH,
		},
		.dst = (struct inst *) (insts->data + insts->size),
	};
	add_bytes(insts, (uint8_t *) &loop, sizeof(loop));
}

void add_store_2_bytes_addr_val(struct insts *insts, uint32_t addr, uint16_t val)
{
	add_load_reg_val(insts, 0, addr);
	add_load_reg_val(insts, 1, val);
	add_store_2_bytes_reg_reg(insts, 0, 1);
}

void add_store_4_bytes_addr_val(struct insts *insts, uint32_t addr, uint32_t val)
{
	add_load_reg_val(insts, 0, addr);
	add_load_reg_val(insts, 1, val);
	add_store_4_bytes_reg_reg(insts, 0, 1);
}

int main(int argc, const char *argv[])
{
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int fd = open("teensy.bin", O_CREAT | O_WRONLY, mode);

	if (fd == -1)
		return 1;

	nvic[0] = 0x20008000;
	nvic[1] = 0x000001BF;
	for (size_t i = 2; i < 111; ++i) {
		nvic[i] = 0x000001BD;
	}

	struct insts insts = {
		.size = 0,
		.capacity = sizeof(insts.data),
	};
	add_infinite_loop(&insts);

	struct context context = {
		.start_addr = 0x000001BC,
		.pos = context.buf,
		.end = context.buf + ARRAY_SIZE(context.buf),
		.literal_pool_size = 0,
	};

	generate_insts(&context, &insts);

	insts.size = 0;

	add_load_reg_val(&insts, 0, 0x4005200E);
	add_load_reg_val(&insts, 1, 0x0000C520);
	add_load_reg_val(&insts, 2, 0x0000D928);
	add_store_2_bytes_reg_reg(&insts, 0, 1);
	add_store_2_bytes_reg_reg(&insts, 0, 2);
	add_store_2_bytes_addr_val(&insts, 0x40052000, 0x0010);
	add_store_4_bytes_addr_val(&insts, 0x40048030, 0x09000000);
	add_store_4_bytes_addr_val(&insts, 0x40048038, 0x00043F82);
	add_store_4_bytes_addr_val(&insts, 0x4004803C, 0x2B000001);
	add_infinite_loop(&insts);

	generate_insts(&context, &insts);

	if (write(fd, nvic, sizeof(nvic)) != sizeof(nvic))
		return 1;

	ssize_t num_bytes = context.pos - context.buf;
	if (write(fd, context.buf, num_bytes) != num_bytes)
		return 1;

	close(fd);

	return 0;
}
