# Symbol hashmap

`hashmap.c` / `hashmap.h` provide a standalone C container. Include `hashmap.h`
and compile/link `hashmap.c` with the interpreter's object implementation. It is
used by lexical scope lookup and list dictionaries on this branch.

```c
skH *scope = skH_new(parent);  /* parent may be NULL */
skO *name = skO_quoted_symbol_new("answer");
skO *value = skO_number_new(42);
const skH_binding *binding;

if (!scope || !skH_set(scope, name->data.sym, value, SKH_VALUE)) {
    skO_free(value);  /* unsuccessful insertion did not consume it */
    /* Handle failure. */
} else {
    binding = skH_get(scope, name->data.sym);
    /* binding->value is borrowed. Clone it if a stack needs an owned copy. */
}
skO_free(name);
skH_free(scope);
```

Keys are interned `symbol *`, obtained from either Symbol or QuotedSymbol values.
The symbol wrapper is not consumed. Keys are borrowed from the intentional global
symbol table. Names must remain immutable; equality uses pointer identity.
Use interpreter-interned symbols, not independently allocated symbol structs.

Every entry owns one detached `skO` (`next == NULL`), including all its list
contents. All Shirka value types are supported. Distinct entries must not share
owned objects; clone explicitly when required. Borrowed lookup results must not
be modified or freed. Replacing a local entry releases its old value. Reinstalling
that exact value pointer in the same entry is also supported (e.g. to change its
mode); it is not freed first.

Each binding carries `SKH_VALUE` or `SKH_OPERATION`. These record the intended
`->` / `=>` distinction without coupling storage to evaluation:

- Value lookup allows the caller to clone the borrowed value for an ordinary
  reference, or use `skH_take` to transfer ownership for a restore/move (`<-`).
- An operation binding identifies a value intended for execution. Lookup itself
  never executes it. A future evaluator must clone/transfer instructions according
  to its ownership contract and validate callability. The hashmap does not impose
  the current evaluator's list-only operation rule.

`skH_set` replaces or inserts locally, even when an ancestor has the same key.
It returns 1 on success; 0 on allocation failure, invalid mode, NULL argument, or
an attached value. On failure the map and caller's value are unchanged.
`skH_getLocal` inspects only the map. `skH_get` searches it, then each direct
ancestor until a binding is found or the chain ends (implemented iteratively to
avoid C recursion). `skH_take` removes only a local entry and returns its owned
value; it optionally returns the binding mode. Missing entries return NULL.
Removing a shadow exposes the ancestor again. Failure leaves the mode output
unchanged. The map never inserts placeholder NULL values.

The optional ancestor pointer is private and immutable after construction. This
prevents ancestor cycles through the public API. It is borrowed: destroy
children before ancestors, and never use a map after freeing its ancestor.
`skH_free` releases local entries and their values, not ancestors or symbols.
There is no reference counting or garbage collector.

The implementation uses FNV-1a over symbol-name bytes, separate chaining,
power-of-two bucket arrays (initially 16), and doubling at 75% occupancy. Nodes
remain in place while growing, so borrowed binding pointers survive resizing;
replacement, removal, and destruction invalidate the affected borrowed binding.
Ordinary local lookup/insertion/removal is expected O(1), worst-case O(n) under
collisions; a lookup across d ancestors includes their lookup costs. Growth is
O(n), amortized across insertions. Symbol hashing is O(name length), bounded by
Shirka's symbol limit. This is not a collision-resistant hash for hostile input.
The map does not shrink automatically. Allocation-size overflow is checked before
growth. No environment, error handler, or interpreter scope is needed by the API.

Run `make test-hashmap` (also part of `make test`). Tests cover growth and bucket
collisions, stable pointers, all value types, three-level lookup, shadowing,
restoration of ancestors, ownership transfer, replacement, and failed allocations.
Only hashmap allocations are counted by this test's injected allocator; the
interpreter's intentional symbol retention is unchanged.

## Lexical-scoping branch extensions

The interpreter's snapshot wrapper is `skL` (`scope.c`). It owns the lifetime of
lexical ancestors while `skH` continues borrowing ancestor maps. `skH_copy`
clones local entries and retains the same borrowed ancestor pointer; its caller
must preserve that ancestor's lifetime. Copy failure releases partial entries.

Two additional kinds support lexical dictionaries: `SKH_NATIVE` has a static
`native` function pointer and NULL value; `SKH_HIDDEN` has NULL value and stops
lookup without falling through to ancestors. `skH_setNative` and `skH_hide`
create these entries. `skH_set` still accepts only the original value/operation
kinds. `skH_get` returns hidden entries so callers can distinguish a masked name
from an absent local entry. `skH_take` can remove these entries too, returning
NULL and setting the kind output; use lookup first when that distinction matters.

`skH_each(map, visit, data)` visits local entries in unspecified order, including
native and hidden entries. The callback receives a borrowed binding and returns
nonzero to continue, zero to stop. The function returns one if traversal completed,
zero if stopped. Do not mutate the map during visitation. Ancestor traversal and
shadow filtering belong to the caller.

`skE_lookup(env, key)` borrows a visible binding using the evaluator's normal
lookup, including dotted and stack-relative paths, without copying or executing
it. Relative paths validate the top stack receiver and may raise an interpreter
error if it is absent or not a list. Other missing paths return NULL. Binding
lifetime follows the hashmap borrowing rules above.
