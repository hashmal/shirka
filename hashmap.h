#ifndef HASHMAP_H
#define HASHMAP_H

#include "shirka.h"


typedef enum {
	SKH_VALUE,
	SKH_OPERATION,
	SKH_NATIVE,
	SKH_HIDDEN
} skH_kind;

struct skH_binding {
	symbol *key;
	skO *value;
	skH_kind kind;
	skE_natOp *native;
};

/* The optional ancestor is borrowed, immutable, and must outlive this map.
 * Allocation failure returns NULL. Freeing NULL is allowed. */
skH *skH_copy (const skH *map);
int skH_setNative (skH *map, symbol *key, skE_natOp *native);
int skH_hide (skH *map, symbol *key);

skH *skH_new (skH *ancestor);
void skH_free (skH *map);

/* Install/replace a local binding. Success transfers ownership of a detached
 * value (next == NULL); failure returns 0 and leaves ownership with the caller.
 * Keys are borrowed interned symbols. No copying or execution happens here. */
int skH_set (skH *map, symbol *key, skO *value, skH_kind kind);

/* Borrowed bindings: stable across growth, invalidated by replacement, take,
 * or destruction of their owning map. NULL means absent. get searches ancestors;
 * getLocal and take operate only on the specified map. */
const skH_binding *skH_get (const skH *map, const symbol *key);
const skH_binding *skH_getLocal (const skH *map, const symbol *key);

/* Visit local entries in unspecified order, including hidden/native entries.
 * Returning zero stops iteration. Do not mutate the map during visitation.
 * Returns one if all entries were visited, zero if stopped by the visitor. */
int skH_each (const skH *map, int (*visit)(const skH_binding *, void *), void *data);

/* Remove a local binding and return its owned value, or NULL if absent.
 * If supplied, kind receives the binding mode on success only. */
skO *skH_take (skH *map, const symbol *key, skH_kind *kind);

#endif
