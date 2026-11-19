#include <stdlib.h>
#include <stdint.h>
#include "hashmap.h"

skL *skL_retain (skL *scope)
{
	if (scope) {
		if (scope->references == SIZE_MAX)
			FATAL("PANIC! Too many lexical references.\n");
		scope->references++;
	}
	return scope;
}

skL *skL_new (skL *ancestor)
{
	skL *scope = malloc(sizeof(skL));

	if (!scope)
		FATAL("PANIC! Could not allocate lexical scope.\n");
	scope->references = 1;
	scope->ancestor = skL_retain(ancestor);
	scope->map = skH_new(ancestor ? ancestor->map : NULL);
	if (!scope->map)
		FATAL("PANIC! Could not allocate lexical dictionary.\n");
	return scope;
}

void skL_free (skL *scope)
{
	if (!scope || --scope->references)
		return;
	skH_free(scope->map);
	skL_free(scope->ancestor);
	free(scope);
}

void skL_write (skL **scope)
{
	skL *copy;

	if ((*scope)->references == 1)
		return;
	copy = malloc(sizeof(skL));
	if (!copy)
		FATAL("PANIC! Could not copy lexical scope.\n");
	copy->references = 1;
	copy->ancestor = skL_retain((*scope)->ancestor);
	copy->map = skH_copy((*scope)->map);
	if (!copy->map)
		FATAL("PANIC! Could not copy lexical dictionary.\n");
	skL_free(*scope);
	*scope = copy;
}
