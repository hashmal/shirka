/* Native operand validation. Run from the repository root:
 * cc -std=c99 -g -fsanitize=address,undefined -I. \
 *    test/runtime.c env.c parser.c objects.c -lm -o /tmp/shirka-runtime-test
 * /tmp/shirka-runtime-test
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "shirka.h"

skE_natOp skI_add, skI_sub, skI_mul, skI_div, skI_pow, skI_mod;
skE_natOp skI_gt, skI_lt, skI_and, skI_or, skI_abs, skI_not;
skE_natOp skI_length, skI_cons, skI_uncons, skI_quote, skI_unquote;
skE_natOp skI_parse, skI_with, skI_exec, skI_exec_if, skI_try;
skE_natOp skI_defObject, skI_defOperation, skI_undef, skI_eql;

static skO *value (skO_t type)
{
	switch (type) {
	case SKO_NUMBER:    return skO_number_new(1);
	case SKO_BOOLEAN:   return skO_boolean_new(1);
	case SKO_CHARACTER: return skO_character_new('a');
	case SKO_SYMBOL:    return skO_symbol_new("x");
	case SKO_QSYMBOL:   return skO_quoted_symbol_new("x");
	case SKO_LIST:      return skO_list_new();
	}
	abort();
}

static void clear_stack (skE *env)
{
	while (env->stack)
		skO_free(skE_stackPop(env));
}

static void expect_error (skE *env, skE_natOp *op)
{
	skO *top = env->stack;
	skO *second = top ? top->next : NULL;

	if (setjmp(env->jmp)) {
		/* Validation must not consume or reorder operands. */
		assert(env->stack == top);
		assert(!top || top->next == second);
		clear_stack (env);
		return;
	}
	op(env);
	assert(0);
}

static void check_binary (skE *env, skE_natOp *op, skO_t type)
{
	int tag;

	for (tag = SKO_NUMBER; tag <= SKO_LIST; tag++) {
		if (tag == (int)type)
			continue;
		skE_stackPush(env, value (type));
		skE_stackPush(env, value ((skO_t)tag));
		expect_error (env, op);
		skE_stackPush(env, value ((skO_t)tag));
		skE_stackPush(env, value (type));
		expect_error (env, op);
	}
	expect_error (env, op);
	skE_stackPush(env, value (type));
	expect_error (env, op);
}

int main (void)
{
	skE *env = skE_new();
	skE_natOp *numbers[] = {
		skI_add, skI_sub, skI_mul, skI_div, skI_pow, skI_mod,
		skI_gt, skI_lt
	};
	skE_natOp *lists[] = {
		skI_length, skI_uncons, skI_parse, skI_with, skI_exec, skI_try
	};
	skE_natOp *symbols[] = { skI_quote, skI_unquote };
	skO *list;
	skO *sym;
	symbol *identity;
	size_t i;
	int tag;

	skE_init(env);
	for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
		check_binary (env, numbers[i], SKO_NUMBER);
	check_binary (env, skI_and, SKO_BOOLEAN);
	check_binary (env, skI_or, SKO_BOOLEAN);

	for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
		for (tag = SKO_NUMBER; tag < SKO_LIST; tag++) {
			skE_stackPush(env, value ((skO_t)tag));
			expect_error (env, lists[i]);
		}
		expect_error (env, lists[i]);
	}
	for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
		for (tag = SKO_NUMBER; tag <= SKO_LIST; tag++) {
			if (tag == SKO_SYMBOL || tag == SKO_QSYMBOL)
				continue;
			skE_stackPush(env, value ((skO_t)tag));
			expect_error (env, symbols[i]);
		}
		expect_error (env, symbols[i]);
	}

	skE_stackPush(env, skO_list_new());
	expect_error (env, skI_uncons);
	skE_stackPush(env, skO_number_new(1));
	skE_stackPush(env, skO_number_new(2));
	expect_error (env, skI_cons);
	skE_stackPush(env, skO_list_new());
	expect_error (env, skI_cons);
	expect_error (env, skI_eql);
	skE_stackPush(env, skO_number_new(1));
	expect_error (env, skI_eql);
	skE_stackPush(env, skO_list_new());
	expect_error (env, skI_abs);
	skE_stackPush(env, skO_number_new(1));
	expect_error (env, skI_not);
	skE_stackPush(env, skO_number_new(1));
	skE_stackPush(env, skO_list_new());
	expect_error (env, skI_exec_if);
	skE_stackPush(env, skO_boolean_new(1));
	skE_stackPush(env, skO_number_new(1));
	expect_error (env, skI_exec_if);

	for (tag = SKO_NUMBER; tag <= SKO_LIST; tag++) {
		if (tag == SKO_QSYMBOL)
			continue;
		skE_stackPush(env, skO_list_new());
		skE_stackPush(env, value ((skO_t)tag));
		expect_error (env, skI_defOperation);
		skE_stackPush(env, skO_number_new(42));
		skE_stackPush(env, value ((skO_t)tag));
		expect_error (env, skI_defObject);
		skE_stackPush(env, value ((skO_t)tag));
		expect_error (env, skI_undef);
	}
	skE_stackPush(env, skO_number_new(42));
	skE_stackPush(env, skO_quoted_symbol_new("x"));
	expect_error (env, skI_defOperation);

	for (tag = SKO_NUMBER; tag <= SKO_LIST; tag++) {
		if (tag == SKO_CHARACTER)
			continue;
		list = skO_list_new();
		sk_list_append(list, skO_character_new('a'));
		sk_list_append(list, value ((skO_t)tag));
		skE_stackPush(env, list);
		expect_error (env, skI_parse);
		list = skO_list_new();
		sk_list_append(list, value ((skO_t)tag));
		skE_stackPush(env, list);
		expect_error (env, skI_with);
	}
	list = skO_list_new();
	sk_list_append(list, skO_character_new(0));
	skE_stackPush(env, list);
	expect_error (env, skI_parse);
	list = skO_list_new();
	sk_list_append(list, skO_character_new(0));
	skE_stackPush(env, list);
	expect_error (env, skI_with);

	/* Valid calls still move values and preserve symbol identity. */
	skE_stackPush(env, skO_number_new(2));
	skE_stackPush(env, skO_number_new(3));
	skI_add(env);
	assert(env->stack->data.number == 5);
	clear_stack (env);
	sym = skO_quoted_symbol_new("x");
	identity = sym->data.sym;
	skE_stackPush(env, sym);
	skI_quote(env);
	skI_quote(env);
	skI_unquote(env);
	skI_unquote(env);
	assert(env->stack == sym && sym->tag == SKO_SYMBOL);
	assert(sym->data.sym == identity);
	clear_stack (env);
	skE_free(env);
	puts("Runtime validation regressions passed.");
	return 0;
}
