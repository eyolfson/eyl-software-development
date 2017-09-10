#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof((a))/sizeof((a)[0]))

static uint32_t nvic[111];

/*
Branch
*/

enum inst_kind {
  BRANCH,
};

struct inst {
	enum inst_kind kind;
};

struct branch_inst {
	struct inst inst;
	struct inst *dst;
};

struct context {
	uint32_t next_addr;
	uint8_t buf[4];
	uint8_t *pos;
	uint8_t *end;
};

void generate_insts(struct context *c, struct inst *insts, size_t insts_size)
{
	uint32_t addr = c->next_addr;
	assert(insts_size == 1);
	assert(insts[0].kind == BRANCH);
	struct branch_inst *bi = (struct branch_inst *) &(insts[0]);
	assert(bi == (struct branch_inst *) bi->dst);
	*c->pos = 0xFE;
	++c->pos;
	*c->pos = 0xE7;
	++c->pos;
}

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

	struct branch_inst todo = {
		.inst = {
			.kind = BRANCH,
		},
		.dst = &(todo.inst),
	};

	generate_insts(&context, &(unused.inst), 1);
	generate_insts(&context, &(todo.inst), 1);
	assert(context.pos == context.end);

	if (write(fd, nvic, sizeof(nvic)) != sizeof(nvic))
		return 1;

	if (write(fd, context.buf, sizeof(context.buf)) != sizeof(context.buf))
		return 1;

	close(fd);

	return 0;
}
