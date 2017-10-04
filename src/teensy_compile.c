#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof((a))/sizeof((a)[0]))

static uint32_t nvic[111];

enum inst_kind {
	BRANCH,
	LOAD,
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

struct context {
	uint32_t start_addr;
	uint8_t buf[256];
	uint8_t *pos;
	uint8_t *end;
	struct load_inst *literal_pool[2];
	uint8_t literal_pool_size;
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

void generate_inst(struct context *c, struct inst *inst)
{
	switch (inst->kind) {
	case BRANCH:
		generate_branch_inst(c, (struct branch_inst *) inst);
		break;
	case LOAD:
		generate_load_inst(c, (struct load_inst *) inst);
		break;
	}
}

void generate_insts(struct context *c, struct inst **insts, size_t insts_size)
{
	for (size_t i = 0; i < insts_size; ++i) {
		generate_inst(c, insts[i]);
	}

	generate_literal_pool(c);
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

	struct context context = {
		.start_addr = 0x000001BC,
		.pos = context.buf,
		.end = context.buf + ARRAY_SIZE(context.buf),
		.literal_pool_size = 0,
	};

	struct branch_inst unused = {
		.inst = {
			.kind = BRANCH,
		},
		.dst = &(unused.inst),
	};
	struct load_inst load_wdog_unlock_addr = {
		.inst = {
			.kind = LOAD,
		},
		.reg = 0,
		.value = 0x4005200E,
	};
	struct branch_inst infinite_loop = {
		.inst = {
			.kind = BRANCH,
		},
		.dst = &(infinite_loop.inst),
	};

	struct inst *unused_insts[] = {
		&(unused.inst),
	};
	struct inst *reset_insts[] = {
		&(load_wdog_unlock_addr.inst),
		&(infinite_loop.inst),
	};

	generate_insts(&context, unused_insts, ARRAY_SIZE(unused_insts));
	generate_insts(&context, reset_insts, ARRAY_SIZE(reset_insts));

	if (write(fd, nvic, sizeof(nvic)) != sizeof(nvic))
		return 1;

	ssize_t num_bytes = context.pos - context.buf;
	if (write(fd, context.buf, num_bytes) != num_bytes)
		return 1;

	close(fd);

	return 0;
}
