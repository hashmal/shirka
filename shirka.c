/* Copyright (C) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>

#include "shirka.h"

int main (int argc, char const *argv[])
{
	FILE   *f;
	size_t f_size;
	char   *src;
	char   *cursor;
	skO *ast;
	skE *env;

	f = fopen(argv[1], "rb");
	if (f == NULL) {
		printf("PANIC! Could not open file.\n");
		exit(EXIT_FAILURE);
	}
	/* get file size */
	fseek(f, 0, SEEK_END);
	f_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	/* copy source into string */
	src = (char *)malloc(f_size);
	fread(src, f_size, 1, f);
	fclose(f);

	cursor = src;
	ast = skO_parse(&cursor);

	free(src);

	env = skE_new();

	skE_execList(env, ast);

	skE_free(env);
	return 0;
}
