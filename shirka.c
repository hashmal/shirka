/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>

#include "shirka.h"

int main (int argc, char const *argv[])
{
	skO *ast;
	skE *env = skE_new();
	skE_init(env);

	if (setjmp(env->jmp)) {
		printf("Panic mode was set. Aborting.\n");
		exit(EXIT_FAILURE);
	}

	if (argc != 2) {
		puts("Wrong number of command line arguments.");
		env->panic = 1;
		exit(EXIT_FAILURE);
	}

	ast = skO_loadParse("lib/prelude.shk");
	skE_execList(env, ast, 0);

	ast = skO_loadParse((char *)argv[1]);
	skE_execList(env, ast, 1);

	if (env->stack)
		printf("WARNING! Stack non empty upon exit.\n");

	if (env->panic) {
		skE_free(env);
		exit(EXIT_FAILURE);
	} else {
		skE_free(env);
		return 0;
	}
}
