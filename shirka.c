/* Copyright (C) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>

#include "shirka.h"

skO *load_parse (char *path)
{
	FILE   *f;
	size_t f_size;
	char   *src;
	char   *cursor;
	skO    *ast;

	f = fopen(path, "rb");
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
	src[f_size] = 0;
	fclose(f);

	cursor = src;
	ast = skO_parse(&cursor);

	free(src);

	return ast;
}

int main (int argc, char const *argv[])
{
	skO *ast;
	skE *env = skE_new();

	if (argc != 2) {
		puts("Wrong number of command line arguments.");
		exit(EXIT_FAILURE);
	}

	ast = load_parse("lib/prelude.shk");
	skE_execList(env, ast, 0);

	ast = load_parse((char *)argv[1]);
	skE_execList(env, ast, 1);

	skE_free(env);
	return 0;
}
