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
	uint32_t next_addr;
	uint8_t buf[4];
	uint8_t *pos;
	uint8_t *end;
};

void generate_branch_inst(struct context *c, struct branch_inst *inst)
{
	uint32_t addr = c->next_addr;
	assert((struct inst *) inst == inst->dst);
	*c->pos = 0xFE;
	++c->pos;
	*c->pos = 0xE7;
	++c->pos;
}

void generate_load_inst(struct context *c, struct load_inst *inst)
{
}

void generate_insts(struct context *c, struct inst **insts, size_t insts_size)
{
	for (size_t i = 0; i < insts_size; ++i) {
		switch (insts[i]->kind) {
		case BRANCH:
			generate_branch_inst(c, (struct branch_inst *) insts[i]);
			break;
		case LOAD:
			generate_load_inst(c, (struct load_inst *) insts[i]);
			break;
		}
	}
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
		.next_addr = 0x000001BC,
		.pos = context.buf,
		.end = context.buf + ARRAY_SIZE(context.buf),
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
		.reg = 3,
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
	assert(context.pos == context.end);

	if (write(fd, nvic, sizeof(nvic)) != sizeof(nvic))
		return 1;

	if (write(fd, context.buf, sizeof(context.buf)) != sizeof(context.buf))
		return 1;

	close(fd);

	return 0;
}
