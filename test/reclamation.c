/* Allocation-balance regressions: make test-reclamation
 * Only the interpreter sources use the malloc/free wrappers; this file calls
 * the real allocator. Interned symbols intentionally remain allocated.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "shirka.h"
#include "reclamation.h"

static size_t allocations;

void *sk_test_alloc (size_t size)
{
	void *ptr = malloc(size);

	assert(ptr);
	allocations++;
	return ptr;
}

void sk_test_free (void *ptr)
{
	if (ptr) {
		assert(allocations);
		allocations--;
	}
	free(ptr);
}

static void parse (char *source, int valid)
{
	char *cursor = source;
	jmp_buf error;
	skO *list;

	if (setjmp(error)) {
		assert(!valid);
		return;
	}
	list = skO_parse(&cursor, error, NULL);
	skO_free(list);
	assert(valid);
}

static void run (char *source, int valid)
{
	skE *env = skE_new();
	char *cursor = source;
	skO *list;

	skE_init(env);
	if (setjmp(env->jmp)) {
		skE_free(env);
		assert(!valid);
		return;
	}
	list = skO_parse(&cursor, env->jmp, NULL);
	skE_execList(env, list, 1);
	skE_free(env);
	assert(valid);
}

static void exercise (void)
{
	skE *env;

	run("1 2 3 [4 [5]]", 1);
	run("\"1 2 3\" $parse", 1);
	run("\"lib/prelude.shk\" with 10 1 .. [+] 0 fold", 1);
	run("[1 -> x [2 -> y 3]] ! 4", 1);
	run("1 missing 2 [3]", 0);
	run("[1 -> x [2 -> y missing 3] ! 4] ! 5", 0);
	run("[1 2 TRUE + 3] try", 1);
	run("[[1 missing 2] try 3 missing 4] try", 1);
	run("[[1 missing] try 2] try", 1);
	run("[[] => f [] => f] try", 1);
	run("[<- missing] try", 1);
	run("[[] => f <- f] try", 1);
	run("42 -> x [x 1 missing 2] try x", 1);
	run("[\"[ : ]\" $parse 1] try", 1);
	run("[\"test/fixtures/absent.shk\" with] try", 1);
	run("[\"test/fixtures/malformed.shk\" with] try", 1);
	run("[\"test/fixtures\" with] try", 1);
	run("[1] => f f 2 f", 1);
	run("[1] => f f", 1);

	/* Includes unused scopes, unconsumed stack data, and skE_call's transfer. */
	env = skE_new();
	skE_init(env);
	if (setjmp(env->jmp))
		abort();
	skE_scopePush(env);
	skE_defObject(env, skO_quoted_symbol_new("x"), skO_list_new());
	skE_scopePush(env);
	skE_stackPush(env, skO_number_new(42));
	skE_call(env, skO_symbol_new("TRUE"));
	skE_free(env);
}

int main (void)
{
	size_t baseline;
	int i;

	/* No symbols: all parser allocations must be reclaimed. */
	for (i = 0; i < 100; i++) {
		parse("1 2 3", 1);
		parse("[1 [2]] \"abc\"", 1);
		parse("[ : ]", 0);
		parse("(1) [ : ]", 0);
		assert(allocations == 0);
	}

	/* Interning is intentional. Warm all names before measuring repetitions. */
	exercise();
	baseline = allocations;
	for (i = 0; i < 100; i++) {
		exercise();
		assert(allocations == baseline);
	}
	puts("Reclamation regressions passed; interned symbols retained.");
	return 0;
}
