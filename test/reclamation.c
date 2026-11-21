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
	run("\"lib/prelude.shk\" with "
		"[-> n [n +]] => make-adder 10 make-adder => add-ten 5 add-ten", 1);
	run("\"lib/prelude.shk\" with "
		"[ [recur] ] => recur recur !", 1);
	run("\"lib/prelude.shk\" with "
		"10 -> x [[x]] -> branches 20 -> branches.x branches uncons >< << !", 1);
	run("[] -> record 42 -> record.x <- record.x record.x", 0);
	run("[] -> record [] -> record.x 42 -> record.x.value "
		"record.x -> record.x.y", 1);
	run("[] -> record [] -> record.x <- record.x -> record.x.y", 0);
	run("\"lib/prelude.shk\" with \"lib/ansi.shk\" with "
		"\"a\" \"b\" [:bold :blue] ansi-style ++ [:dim :red] ansi-style", 1);
	parse("'\\0334 \"\\0x\" '\\377", 1);
	parse("'\\400", 0);
	parse("record.child.x :record.child.x -> record.child.x", 1);
	parse("record..x", 0);
	parse("(puts) [record.]", 0);
	run("\"lib/prelude.shk\" with \"lib/test.shk\" with "
		"[[1 1] assert_equal [abort] assert_error] test/run", 1);
	run("\"lib/prelude.shk\" with \"lib/test.shk\" with "
		"[[[1 2] assert_equal] test/run] try", 1);
	run("\"1 2 3\" $parse", 1);
	run("\"lib/prelude.shk\" with [1 2 3 4 5 6 7 8 9 10] [+] 0 fold", 1);
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

/* A persistent host resumes after each submission, just as ripple does. */
static void submit (skE *env, char *source)
{
	context *scope = env->scope;
	skO *list;

	if (setjmp(env->jmp)) {
		skE_unwind(env, scope);
		return;
	}
	if (skO_read(source, &list) != SK_PARSE_OK)
		return;
	sk_list_append(list, skO_list_new());
	skE_execList(env, list, 0);
	skO_free(skE_stackPop(env));
	assert(env->scope == scope && !env->execution);
}

static void persistent_session (void)
{
	skE *env = skE_new();
	context *scope;
	size_t baseline;
	int i;

	skE_init(env);
	skE_scopePush(env);
	scope = env->scope;
	submit(env, "42 -> persistent [1 +] => increment");
	/* Warm symbols before checking the allocation count. */
	submit(env, "[1 -> temporary missing 2] !");
	baseline = allocations;
	for (i = 0; i < 100; i++) {
		submit(env, "1 increment");
		assert(env->stack->data.number == 2);
		skO_free(skE_stackPop(env));
		submit(env, "[1 -> temporary missing 2] !");
		submit(env, "1 TRUE +");
		assert(env->stack->tag == SKO_BOOLEAN);
		while (env->stack)
			skO_free(skE_stackPop(env));
		submit(env, "[ : ]");
		assert(env->scope == scope && !env->execution);
		assert(allocations == baseline);
	}
	/* Replacing scalar reserves must not retain dictionary version history. */
	for (i = 0; i < 100; i++) {
		submit(env, "99 -> persistent");
		assert(env->scope == scope && !env->execution);
		assert(allocations == baseline);
	}
	submit(env, "42 -> persistent");
	submit(env, "persistent");
	assert(env->stack->data.number == 42);
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
	persistent_session();
	exercise();
	baseline = allocations;
	for (i = 0; i < 100; i++) {
		exercise();
		assert(allocations == baseline);
	}
	puts("Reclamation regressions passed; interned symbols retained.");
	return 0;
}
