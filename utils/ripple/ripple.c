#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ripple.h"

#ifndef RIPPLE_HOME
#define RIPPLE_HOME "."
#endif

struct ripple ripple;
volatile sig_atomic_t ripple_interrupt;
volatile sig_atomic_t ripple_stop;

/* Validate before consuming the controller's character list. */
static char *text_pop (skE *env)
{
	skO *text = skE_stackPeek(env, 0);
	skO *node;
	char *buffer;
	size_t length = 0;

	skE_checkType(env, text, SKO_LIST);
	for (node = text->data.list; node; node = node->next) {
		skE_checkType(env, node, SKO_CHARACTER);
		if (!node->data.character || length == RIPPLE_INPUT_LIMIT)
			skE_error(env, "ripple: text contains NUL or exceeds 1 MiB");
		length++;
	}
	buffer = malloc(length + 1);
	if (!buffer)
		skE_error(env, "ripple: could not allocate text");
	length = 0;
	for (node = text->data.list; node; node = node->next)
		buffer[length++] = node->data.character;
	buffer[length] = 0;
	skO_free(skE_stackPop(env));
	return buffer;
}

static skO *text_new (const char *text)
{
	skO *list = skO_list_new();
	skO **tail = &list->data.list;

	while (text && *text) {
		*tail = skO_character_new(*text++);
		tail = &(*tail)->next;
	}
	return list;
}

/* Own list, or load path; recover on the actual session stack, never try's. */
static int evaluate (skO *list, char *path)
{
	skE *env = ripple.session;

	if (ripple_stop || ripple_interrupt) {
		if (list)
			skO_free(list);
		ripple_interrupt = 0;
		return 0;
	}
	if (setjmp(env->jmp)) {
		skE_unwind(env, ripple.scope);
		ripple_interrupt = 0;
		return 0;
	}
	if (path)
		list = skO_loadParse(env, path);
	/* A real continuation prevents a final callee from inheriting session scope.
	 * This private literal cannot be replaced by a user-defined operation. */
	sk_list_append(list, skO_list_new());
	skE_execList(env, list, 0);
	skO_free(skE_stackPop(env));
	ripple_interrupt = 0;
	return 1;
}

/* prompt -- text :line|:cancel|:eof|:invalid|:error */
static skO *read_line (skE *env)
{
	char *prompt = text_pop(env);
	enum ripple_input status;
	char *text;
	const char *names[] = {"line", "cancel", "eof", "invalid", "error"};

	text = ripple_read_line(prompt, &status);
	free(prompt);
	skE_stackPush(env, text_new(text));
	free(text);
	skE_stackPush(env, skO_quoted_symbol_new((char *)names[status]));
	return NULL;
}

/* text -- instructions :ready|:more|:invalid */
static skO *parse (skE *env)
{
	char *text = text_pop(env);
	skO *list;
	int status = skO_read(text, &list);

	free(text);
	skE_stackPush(env, list ? list : skO_list_new());
	skE_stackPush(env, skO_quoted_symbol_new(status == SK_PARSE_OK ? "ready" :
		status == SK_PARSE_MORE ? "more" : "invalid"));
	return NULL;
}

/* instructions -- success? */
static skO *execute (skE *env)
{
	skE_checkType(env, skE_stackPeek(env, 0), SKO_LIST);
	skE_stackPush(env, skO_boolean_new(evaluate(skE_stackPop(env), NULL)));
	return NULL;
}

/* path -- success? */
static skO *load_file (skE *env)
{
	char *path = text_pop(env);
	int success = evaluate(NULL, path);

	free(path);
	skE_stackPush(env, skO_boolean_new(success));
	return NULL;
}

static int view_limit (skE *env, size_t depth, int maximum)
{
	skO *value = skE_stackPeek(env, depth);

	skE_checkType(env, value, SKO_NUMBER);
	if (!(value->data.number >= 1 && value->data.number <= maximum))
		skE_error(env, "ripple: invalid stack inspection limit");
	return (int)value->data.number;
}

/* count width depth -- descriptions omitted? color?; never copies values. */
static skO *stack_view (skE *env)
{
	skO *node = ripple.session->stack;
	skO *list;
	skO **tail;
	char value[512];
	size_t count = 0;
	int depth = view_limit(env, 0, 6);
	int width = view_limit(env, 1, 511);
	int limit = view_limit(env, 2, 256);

	list = skO_list_new();
	tail = &list->data.list;
	skO_free(skE_stackPop(env));
	skO_free(skE_stackPop(env));
	skO_free(skE_stackPop(env));
	while (node && count++ < (size_t)limit) {
		skO_inspect(node, value, (size_t)width + 1, (unsigned)depth);
		*tail = text_new(value);
		tail = &(*tail)->next;
		node = node->next;
	}
	skE_stackPush(env, list);
	skE_stackPush(env, skO_boolean_new(node != NULL));
	skE_stackPush(env, skO_boolean_new(ripple.color));
	return NULL;
}

static skO *clear_stack (skE *env)
{
	(void)env;
	while (ripple.session->stack)
		skO_free(skE_stackPop(ripple.session));
	return NULL;
}

/* text -- ; storage is native, deciding what to remember is self-hosted. */
static skO *remember (skE *env)
{
	char *text = text_pop(env);

	ripple_history(text);
	free(text);
	return NULL;
}

int main (int argc, char **argv)
{
	skE *control;
	volatile int status = EXIT_SUCCESS;

	(void)argv;
	if (argc != 1) {
		fprintf(stderr, "Usage: ripple (or pipe Shirka input into ripple)\n");
		return EXIT_FAILURE;
	}
	ripple.session = skE_new();
	skE_init(ripple.session);
	control = skE_new();
	skE_init(control);
	if (setjmp(control->jmp)) {
		status = EXIT_FAILURE;
	} else {
		skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/lib/prelude.shk"), 0);
		skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/lib/ansi.shk"), 0);
		if (setjmp(ripple.session->jmp)) {
			status = EXIT_FAILURE;
		} else {
			skE_execList(ripple.session,
				skO_loadParse(ripple.session, RIPPLE_HOME "/lib/prelude.shk"), 0);
			skE_scopePush(ripple.session);
			ripple.scope = ripple.session->scope;
			ripple.session->interrupt = &ripple_interrupt;
			ripple_terminal_init();
			skE_defNative(control, "ripple/read-line", read_line);
			skE_defNative(control, "ripple/parse", parse);
			skE_defNative(control, "ripple/execute", execute);
			skE_defNative(control, "ripple/load-file", load_file);
			skE_defNative(control, "ripple/stack-view", stack_view);
			skE_defNative(control, "ripple/clear-stack", clear_stack);
			skE_defNative(control, "ripple/history", remember);
			skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/utils/ripple/highlight.shk"), 0);
			skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/utils/ripple/ripple.shk"), 0);
			/* The controller leaves its accumulated success flag on top. */
			skE_checkType(control, skE_stackPeek(control, 0), SKO_BOOLEAN);
			if (!ripple.interactive && !control->stack->data.boolean)
				status = EXIT_FAILURE;
		}
	}
	ripple_terminal_free();
	skE_free(control);
	skE_free(ripple.session);
	return status;
}
