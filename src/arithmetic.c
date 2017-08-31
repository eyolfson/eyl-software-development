#include <stdint.h>
#include <stdio.h>

enum kind {
	KIND_Op,
	KIND_Number,
};

enum op_kind {
	OP_Add,
	OP_Subtract,
	OP_Multiply,
};

struct expr {
	enum kind kind;
	union {
		struct {
			enum op_kind kind;
			struct expr *lhs;
			struct expr *rhs;
		} op;
		int64_t num;
	};
};

void expr_print(struct expr *e)
{
	switch (e->kind) {
	case KIND_Number:
		printf("%ld", e->num);
		break;
	case KIND_Op:
		printf("(");
		expr_print(e->op.lhs);
		switch (e->op.kind) {
		case OP_Add:
			printf("+");
			break;
		case OP_Subtract:
			printf("-");
			break;
		case OP_Multiply:
			printf("*");
			break;
		}
		expr_print(e->op.rhs);
		printf(")");
		break;
	}
}

int64_t expr_eval(struct expr *e)
{
	switch (e->kind) {
	case KIND_Number:
		return e->num;
	case KIND_Op: {
		int64_t x = expr_eval(e->op.lhs);
		int64_t y = expr_eval(e->op.rhs);
		switch (e->op.kind) {
		case OP_Add:
			return x + y;
		case OP_Subtract:
			return x - y;
		case OP_Multiply:
			return x * y;
		}
	}
	}
}

void expr_set_op(struct expr *e,
                 enum op_kind kind, struct expr *lhs, struct expr *rhs)
{
	e->kind = KIND_Op;
	e->op.kind = kind;
	e->op.lhs = lhs;
	e->op.rhs = rhs;
}

void expr_set_num(struct expr *e, int64_t num)
{
	e->kind = KIND_Number;
	e->num = num;
}

void test1()
{
	struct expr e1;
	struct expr e2;
	struct expr e3;
	struct expr e4;
	struct expr e5;
	struct expr e6;
	struct expr e7;
	expr_set_num(&e7, 6);
	expr_set_num(&e6, 1);
	expr_set_op(&e5, OP_Multiply, &e6, &e7);
	expr_set_num(&e4, 2);
	expr_set_op(&e3, OP_Subtract, &e4, &e5);
	expr_set_num(&e2, 5);
	expr_set_op(&e1, OP_Add, &e2, &e3);
	expr_print(&e1);
	printf(" = %ld\n", expr_eval(&e1));
}

void test2()
{
	struct expr e1;
	struct expr e2;
	struct expr e3;
	struct expr e4;
	struct expr e5;
	struct expr e6;
	struct expr e7;
	expr_set_num(&e7, 6);
	expr_set_num(&e6, 1);
	expr_set_op(&e5, OP_Multiply, &e6, &e7);
	expr_set_num(&e4, 2);
	expr_set_num(&e3, 5);
	expr_set_op(&e2, OP_Add, &e3, &e4);
	expr_set_op(&e1, OP_Subtract, &e2, &e5);
	expr_print(&e1);
	printf(" = %ld\n", expr_eval(&e1));
}

void test3()
{
	struct expr e1;
	struct expr e2;
	struct expr e3;
	struct expr e4;
	struct expr e5;
	struct expr e6;
	struct expr e7;
	expr_set_num(&e7, 2);
	expr_set_num(&e6, 5);
	expr_set_op(&e5, OP_Add, &e6, &e7);
	expr_set_num(&e4, 1);
	expr_set_op(&e3, OP_Subtract, &e5, &e4);
	expr_set_num(&e2, 6);
	expr_set_op(&e1, OP_Multiply, &e3, &e2);
	expr_print(&e1);
	printf(" = %ld\n", expr_eval(&e1));
}

int main()
{
	test1();
	test2();
	test3();
	return 0;
}
