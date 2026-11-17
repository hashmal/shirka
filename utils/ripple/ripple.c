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

void ripple_evaluate (int file)
{
	skE *env = ripple.session;
	skO *list;
	skO *boundary;

	if (ripple_stop || ripple_interrupt) {
		ripple_interrupt = 0;
		return;
	}
	if (setjmp(env->jmp)) {
		skE_unwind(env, ripple.scope);
		ripple.failed = 1;
		ripple_interrupt = 0;
		return;
	}
	if (file) {
		char *path = ripple.source + 5;
		while (*path == ' ' || *path == '\t')
			path++;
		if (!*path)
			skE_error(env, "Usage: /load PATH");
		list = skO_loadParse(env, path);
	} else {
		list = ripple.pending;
		ripple.pending = NULL;
	}
	/* Returning to the host is a real continuation. Without it, the existing
	 * tail-call rule could install a callee's temporary reserves in the session. */
	boundary = skO_list_new();
	sk_list_append(list, boundary);
	/* The root submission deliberately has no temporary scope. */
	skE_execList(env, list, 0);
	/* The private literal executes last; it is not name-resolved user code. */
	skO_free(skE_stackPop(env));
	ripple_interrupt = 0;
}

static skO *ripple_read_op (skE *env)
{
	skE_stackPush(env, skO_boolean_new(ripple.running && !ripple_stop && ripple_read()));
	return NULL;
}

static skO *ripple_command (skE *env)
{
	const char *command = ripple_command_name(ripple.source);

	skE_stackPush(env, skO_quoted_symbol_new((char *)command));
	return NULL;
}

static skO *ripple_eval (skE *env)
{
	(void)env;
	ripple_evaluate(0);
	return NULL;
}

static skO *ripple_load (skE *env)
{
	(void)env;
	ripple_evaluate(1);
	return NULL;
}

static skO *ripple_stack_op (skE *env)
{
	(void)env;
	ripple_stack();
	return NULL;
}

static skO *ripple_clear (skE *env)
{
	(void)env;
	while (ripple.session->stack)
		skO_free(skE_stackPop(ripple.session));
	return NULL;
}

static skO *ripple_stop_op (skE *env)
{
	(void)env;
	ripple.running = 0;
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
	ripple.running = 1;
	ripple.session = skE_new();
	skE_init(ripple.session);
	control = skE_new();
	skE_init(control);
	if (setjmp(control->jmp)) {
		status = EXIT_FAILURE;
	} else {
		skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/lib/prelude.shk"), 0);
		if (setjmp(ripple.session->jmp)) {
			status = EXIT_FAILURE;
		} else {
			skE_execList(ripple.session,
				skO_loadParse(ripple.session, RIPPLE_HOME "/lib/prelude.shk"), 0);
			skE_scopePush(ripple.session);
			ripple.scope = ripple.session->scope;
			ripple.session->interrupt = &ripple_interrupt;
			ripple_terminal_init();
			skE_defNative(control, "ripple/read", ripple_read_op);
			skE_defNative(control, "ripple/command", ripple_command);
			skE_defNative(control, "ripple/eval", ripple_eval);
			skE_defNative(control, "ripple/load", ripple_load);
			skE_defNative(control, "ripple/stack", ripple_stack_op);
			skE_defNative(control, "ripple/clear", ripple_clear);
			skE_defNative(control, "ripple/stop", ripple_stop_op);
			skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/utils/ripple/ripple.shk"), 0);
		}
	}
	ripple_terminal_free();
	if (ripple.pending)
		skO_free(ripple.pending);
	free(ripple.source);
	skE_free(control);
	skE_free(ripple.session);
	return status || (!ripple.interactive && ripple.failed) ? EXIT_FAILURE : EXIT_SUCCESS;
}
