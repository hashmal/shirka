/* Parser memory-safety regressions. Run from the repository root:
 * cc -std=c99 -g -fsanitize=address,undefined -I. \
 *    test/input.c parser.c objects.c scope.c hashmap.c -o /tmp/shirka-input-test
 * /tmp/shirka-input-test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "shirka.h"

static void check(char *source, int valid)
{
	char *cursor = source;
	jmp_buf error;
	skO *result;

	if (setjmp(error)) {
		assert(!valid);
		return;
	}
	result = skO_parse(&cursor, error, NULL);
	skO_free(result);
	assert(valid);
	assert(*cursor == 0);
}

static void check_comment(char *source, skO_t type)
{
	char *cursor = source;
	jmp_buf error;
	skO *result;
	skO *token;

	if (setjmp(error)) {
		assert(0);
		return;
	}
	result = skO_parse(&cursor, error, NULL);
	token = result->data.list;
	assert(token && !token->next);
	assert(token->tag == type);
	assert(strcmp(token->data.sym->name, "foo") == 0);
	skO_free(result);
}

int main(void)
{
	char token[SYMBOL_MAX_LENGTH + 2];
	char quoted[SYMBOL_MAX_LENGTH + 3];
	char *invalid[] = {
		"\"abc", "\"abc\\", "'", "'\\", "\"\\q\"",
		"[ : ]", "[[ : ]]", "(print) [ : ]", "[1 2", "(puts",
		"(puts)", "]", ")"
	};
	size_t i;

	check("", 1);
	check("1 2 3", 1);
	check("[1 [2]] \"abc\" '\\n", 1);
	check("(=> foo) [42]", 1);
	check_comment("foo-- comment", SKO_SYMBOL);
	check_comment(":foo-- comment", SKO_QSYMBOL);

	memset(token, 'a', SYMBOL_MAX_LENGTH - 1);
	token[SYMBOL_MAX_LENGTH - 1] = 0;
	check(token, 1);
	quoted[0] = ':';
	strcpy(quoted + 1, token);
	check(quoted, 1);
	token[SYMBOL_MAX_LENGTH - 1] = 'a';
	token[SYMBOL_MAX_LENGTH] = 0;
	check(token, 0);
	strcpy(quoted + 1, token);
	check(quoted, 0);
	memset(token, '1', SYMBOL_MAX_LENGTH);
	token[SYMBOL_MAX_LENGTH - 1] = 0;
	check(token, 1);
	token[SYMBOL_MAX_LENGTH - 1] = '1';
	check(token, 0);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		check(invalid[i], 0);
	puts("Input regressions passed.");
	return 0;
}
