#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hashmap.h"

typedef struct entry entry;

struct entry {
	entry *next;
	skH_binding binding;
};

struct skH {
	skH *ancestor;
	entry **buckets;
	size_t capacity;
	size_t count;
};

/* Symbol names are immutable. Equality still uses their interned identity. */
static size_t hash (const symbol *key)
{
	const unsigned char *name = (const unsigned char *)key->name;
	uint32_t value = UINT32_C(2166136261);

	while (*name) {
		value ^= *name++;
		value *= UINT32_C(16777619);
	}
	return (size_t)value;
}

static int resize (skH *map, size_t capacity)
{
	entry **buckets;
	entry *node;
	entry *next;
	size_t i;
	size_t slot;

	if (capacity > SIZE_MAX / sizeof(*buckets))
		return 0;
	buckets = malloc(capacity * sizeof(*buckets));
	if (!buckets)
		return 0;
	memset(buckets, 0, capacity * sizeof(*buckets));
	for (i = 0; i < map->capacity; i++) {
		for (node = map->buckets[i]; node; node = next) {
			next = node->next;
			slot = hash(node->binding.key) & (capacity - 1);
			node->next = buckets[slot];
			buckets[slot] = node;
		}
	}
	free(map->buckets);
	map->buckets = buckets;
	map->capacity = capacity;
	return 1;
}

skH *skH_new (skH *ancestor)
{
	skH *map = malloc(sizeof(skH));

	if (!map)
		return NULL;
	map->ancestor = ancestor;
	map->buckets = NULL;
	map->capacity = 0;
	map->count = 0;
	if (!resize(map, 16)) {
		free(map);
		return NULL;
	}
	return map;
}

void skH_free (skH *map)
{
	entry *node;
	entry *next;
	size_t i;

	if (!map)
		return;
	for (i = 0; i < map->capacity; i++) {
		for (node = map->buckets[i]; node; node = next) {
			next = node->next;
			if (node->binding.value)
				skO_free(node->binding.value);
			free(node);
		}
	}
	free(map->buckets);
	free(map);
}

const skH_binding *skH_getLocal (const skH *map, const symbol *key)
{
	entry *node;

	if (!map || !key)
		return NULL;
	for (node = map->buckets[hash(key) & (map->capacity - 1)]; node; node = node->next)
		if (node->binding.key == key)
			return &node->binding;
	return NULL;
}

const skH_binding *skH_get (const skH *map, const symbol *key)
{
	const skH_binding *binding;

	while (map) {
		binding = skH_getLocal(map, key);
		if (binding)
			return binding;
		map = map->ancestor;
	}
	return NULL;
}

static int set (skH *map, symbol *key, skO *value, skH_kind kind, skE_natOp *native)
{
	entry *node;
	size_t slot;

	if (!map || !key || (value && value->next) ||
		(kind < SKH_VALUE || kind > SKH_HIDDEN))
		return 0;
	slot = hash(key) & (map->capacity - 1);
	for (node = map->buckets[slot]; node; node = node->next) {
		if (node->binding.key == key) {
			if (node->binding.value && node->binding.value != value)
				skO_free(node->binding.value);
			node->binding.value = value;
			node->binding.kind = kind;
			node->binding.native = native;
			return 1;
		}
	}
	node = malloc(sizeof(entry));
	if (!node)
		return 0;
	/* Keep at most three entries per four buckets. Grow before insertion. */
	if (map->count >= map->capacity - map->capacity / 4) {
		if (map->capacity > SIZE_MAX / 2 || !resize(map, map->capacity * 2)) {
			free(node);
			return 0;
		}
		slot = hash(key) & (map->capacity - 1);
	}
	node->binding.key = key;
	node->binding.value = value;
	node->binding.kind = kind;
	node->binding.native = native;
	node->next = map->buckets[slot];
	map->buckets[slot] = node;
	map->count++;
	return 1;
}

skO *skH_take (skH *map, const symbol *key, skH_kind *kind)
{
	entry **link;
	entry *node;
	skO *value;

	if (!map || !key)
		return NULL;
	link = &map->buckets[hash(key) & (map->capacity - 1)];
	while (*link && (*link)->binding.key != key)
		link = &(*link)->next;
	if (!*link)
		return NULL;
	node = *link;
	*link = node->next;
	value = node->binding.value;
	if (kind)
		*kind = node->binding.kind;
	free(node);
	map->count--;
	return value;
}

int skH_set (skH *map, symbol *key, skO *value, skH_kind kind)
{
	if (!value || (kind != SKH_VALUE && kind != SKH_OPERATION))
		return 0;
	return set(map, key, value, kind, NULL);
}

int skH_setNative (skH *map, symbol *key, skE_natOp *native)
{
	return native && set(map, key, NULL, SKH_NATIVE, native);
}

int skH_hide (skH *map, symbol *key)
{
	return set(map, key, NULL, SKH_HIDDEN, NULL);
}

skH *skH_copy (const skH *map)
{
	skH *copy = skH_new(map->ancestor);
	entry *node;
	skO *value;
	size_t i;

	if (!copy)
		return NULL;
	for (i = 0; i < map->capacity; i++) {
		for (node = map->buckets[i]; node; node = node->next) {
			value = node->binding.value ? skO_clone(node->binding.value) : NULL;
			if (!set(copy, node->binding.key, value, node->binding.kind, node->binding.native)) {
				if (value)
					skO_free(value);
				skH_free(copy);
				return NULL;
			}
		}
	}
	return copy;
}

int skH_each (const skH *map, int (*visit)(const skH_binding *, void *), void *data)
{
	entry *node;
	size_t i;

	for (i = 0; i < map->capacity; i++)
		for (node = map->buckets[i]; node; node = node->next)
			if (!visit(&node->binding, data))
				return 0;
	return 1;
}
