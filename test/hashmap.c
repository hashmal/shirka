#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "hashmap.h"
#include "test/hashmap-alloc.h"

static size_t allocations;
static int fail_after;

void *skH_test_alloc (size_t size)
{
	void *pointer;

	if (fail_after && !--fail_after)
		return NULL;
	pointer = malloc(size);
	assert(pointer);
	allocations++;
	return pointer;
}

void skH_test_free (void *pointer)
{
	if (pointer) {
		assert(allocations);
		allocations--;
	}
	free(pointer);
}

static symbol *key (int index)
{
	char name[32];
	skO *object;
	symbol *result;

	snprintf(name, sizeof(name), "hashmap-test-%d", index);
	object = skO_symbol_new(name);
	result = object->data.sym;
	skO_free(object);
	return result;
}

static void growth (void)
{
	skH *map = skH_new(NULL);
	const skH_binding *first;
	skO *value;
	int i;

	assert(map);
	assert(skH_set(map, key(0), skO_number_new(0), SKH_VALUE));
	first = skH_get(map, key(0));
	for (i = 1; i < 500; i++)
		assert(skH_set(map, key(i), skO_number_new(i), SKH_VALUE));
	assert(first == skH_get(map, key(0)));
	for (i = 0; i < 500; i++)
		assert(skH_get(map, key(i))->value->data.number == i);
	for (i = 0; i < 500; i += 2) {
		value = skH_take(map, key(i), NULL);
		assert(value->data.number == i && !value->next);
		skO_free(value);
		assert(!skH_get(map, key(i)));
	}
	for (i = 1; i < 500; i += 2)
		assert(skH_get(map, key(i))->value->data.number == i);
	skH_free(map);
}

static void ancestry (void)
{
	skH *root = skH_new(NULL);
	skH *child = skH_new(root);
	skH *leaf = skH_new(child);
	skH_kind kind;
	skO *list = skO_list_new();
	skO *value;

	assert(root && child && leaf);
	sk_list_append(list, skO_quoted_symbol_new("hello"));
	assert(skH_set(root, key(0), list, SKH_OPERATION));
	assert(skH_get(leaf, key(0))->value == list);
	assert(skH_get(leaf, key(0))->kind == SKH_OPERATION);
	assert(!skH_getLocal(leaf, key(0)) && !skH_take(leaf, key(0), NULL));
	assert(skH_set(child, key(0), skO_symbol_new("hello"), SKH_VALUE));
	assert(skH_get(leaf, key(0))->value->tag == SKO_SYMBOL);
	assert(skH_get(leaf, key(0))->kind == SKH_VALUE);
	assert(skH_getLocal(root, key(0))->value == list);
	value = skH_take(child, key(0), &kind);
	assert(kind == SKH_VALUE && value->tag == SKO_SYMBOL);
	skO_free(value);
	assert(skH_get(leaf, key(0))->value == list);
	assert(skH_set(leaf, key(1), skO_boolean_new(1), SKH_VALUE));
	assert(skH_set(leaf, key(1), skO_character_new('x'), SKH_VALUE));
	assert(skH_set(leaf, key(1), skO_quoted_symbol_new("q"), SKH_VALUE));
	assert(skH_get(leaf, key(1))->value->tag == SKO_QSYMBOL);
	assert(!skH_get(leaf, key(99)));
	skH_free(leaf);
	skH_free(child);
	assert(skH_get(root, key(0))->value == list);
	skH_free(root);
}

static void collisions (void)
{
	skH *map = skH_new(NULL);
	skO *copy;
	skO *value;
	int indices[] = {0, 15, 20}; /* Same initial bucket under FNV-1a. */
	int i;

	assert(map);
	for (i = 0; i < 3; i++)
		assert(skH_set(map, key(indices[i]), skO_number_new(i), SKH_VALUE));
	copy = skO_clone(skH_get(map, key(0))->value);
	assert(copy != skH_get(map, key(0))->value);
	assert(skH_set(map, key(99), copy, SKH_VALUE));
	/* Remove middle, tail, then head of the collision chain. */
	for (i = 1; i >= 0; i--) {
		value = skH_take(map, key(indices[i]), NULL);
		assert(value->data.number == i);
		skO_free(value);
		assert(skH_get(map, key(20))->value->data.number == 2);
	}
	value = skH_take(map, key(20), NULL);
	skO_free(value);
	assert(skH_get(map, key(99))->value == copy);
	skH_free(map);
}

static void failures (void)
{
	skH *map;
	skO *value;
	size_t baseline;
	int i;

	for (i = 1; i <= 2; i++) {
		fail_after = i;
		assert(!skH_new(NULL));
		assert(!allocations);
	}
	map = skH_new(NULL);
	assert(map);
	for (i = 0; i < 12; i++)
		assert(skH_set(map, key(i), skO_number_new(i), SKH_VALUE));
	baseline = allocations;
	value = skO_number_new(42);
	for (i = 1; i <= 2; i++) {
		fail_after = i; /* Entry allocation, then growth allocation. */
		assert(!skH_set(map, key(12), value, SKH_VALUE));
		assert(allocations == baseline && value->data.number == 42);
		assert(!skH_get(map, key(12)));
		assert(skH_get(map, key(0))->value->data.number == 0);
	}
	assert(!skH_set(map, key(12), value, (skH_kind)99));
	assert(!skH_set(NULL, key(12), value, SKH_VALUE));
	assert(!skH_set(map, NULL, value, SKH_VALUE));
	assert(skH_set(map, key(12), value, SKH_VALUE));
	fail_after = 1;
	assert(skH_set(map, key(12), skO_number_new(43), SKH_VALUE));
	fail_after = 0; /* Replacement needs no allocation. */
	skH_free(map);
}

int main (void)
{
	growth(); assert(!allocations);
	ancestry(); assert(!allocations);
	collisions(); assert(!allocations);
	failures(); assert(!allocations);
	skH_free(NULL);
	assert(!skH_get(NULL, key(0)));
	puts("Hashmap lookup, ownership, growth, ancestors, and allocation failures passed.");
	return 0;
}
