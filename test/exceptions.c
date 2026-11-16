/* Exception contracts. Run from the repository root:
 * cc -std=c99 -g -fsanitize=address,undefined -I. \
 *    test/exceptions.c env.c parser.c objects.c -lm -o /tmp/shirka-exceptions
 * /tmp/shirka-exceptions
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "shirka.h"

static skE *run (char *source)
{
	skE *env = skE_new();
	char *cursor = source;
	skO *list;

	skE_init(env);
	if (setjmp(env->jmp)) {
		skE_free(env);
		assert(0);
		return NULL;
	}
	list = skO_loadParse(env, "lib/prelude.shk");
	skE_execList(env, list, 0);
	list = skO_parse(&cursor, env->jmp, NULL);
	skE_execList(env, list, 1);
	return env;
}

static void check_failure (char *action)
{
	char source[512];
	skE *env;
	skO *status;
	skO *result;
	skO *sentinel;

	assert(snprintf(source, sizeof(source), "42 [%s] try", action)
		< (int)sizeof(source));
	env = run(source);
	status = skE_stackPop(env);
	result = skE_stackPop(env);
	sentinel = skE_stackPop(env);
	assert(status->tag == SKO_QSYMBOL);
	assert(strcmp(status->data.sym->name, "$try/failed") == 0);
	assert(result->tag == SKO_LIST && !result->data.list);
	assert(sentinel->tag == SKO_NUMBER && sentinel->data.number == 42);
	assert(!env->stack);
	skO_free(status);
	skO_free(result);
	skO_free(sentinel);
	skE_free(env);
}

int main (void)
{
	skE *env;
	skO *result;

	check_failure("+");
	check_failure("1 TRUE +");
	check_failure("[] uncons");
	check_failure("missing");
	check_failure("\"[ : ]\" $parse");
	check_failure("\"test/fixtures/absent.shk\" with");
	check_failure("\"test/fixtures/malformed.shk\" with");
	check_failure("\"test/fixtures\" with");
	check_failure("[missing] try << << missing");

	env = run("42 [1 2] try");
	assert(strcmp(env->stack->data.sym->name, "$try/ok") == 0);
	result = env->stack->next;
	assert(result->tag == SKO_LIST);
	assert(result->data.list->data.number == 2);
	assert(result->data.list->next->data.number == 1);
	assert(!result->data.list->next->next);
	assert(result->next->data.number == 42);
	skE_free(env);

	env = run("42 [[missing] try] try");
	assert(strcmp(env->stack->data.sym->name, "$try/ok") == 0);
	result = env->stack->next->data.list;
	assert(strcmp(result->data.sym->name, "$try/failed") == 0);
	assert(result->next->tag == SKO_LIST && !result->next->data.list);
	assert(!result->next->next);
	skE_free(env);

	env = run("42 [missing] try [<< 7] rescue");
	assert(env->stack->data.number == 7);
	assert(env->stack->next->data.number == 42);
	assert(!env->stack->next->next);
	skE_free(env);
	env = run("42 [1] try [<< 7] rescue");
	assert(env->stack->tag == SKO_LIST);
	assert(env->stack->data.list->data.number == 1);
	assert(env->stack->next->data.number == 42);
	skE_free(env);
	puts("Exception contract regressions passed.");
	return 0;
}
