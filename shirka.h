/* Copyright (c) 2013, Jeremy Pinat. */

#ifndef SHIRKA_H
#define SHIRKA_H

/*
 * Definitions
 * -----------
 *
 * object:      A runtime entity.
 * operation:   A unit of computation possibly manipulating objects on the
 *              stack and/or triggering side effects. Equivalent to
 *              "functions" in some languages.
 * context:     A set of objects accessible through names at a given point.
 * stack        ...
 * environment: ...
 *
 * Naming conventions
 * ------------------
 *
 * All exported names begin with `sk'.
 *
 * - skO for names related to objects
 * - skE for names related to environments
 */

#include <stdio.h>
#include <setjmp.h>

#define SK_O_TAIL

#define SYMBOL_MAX_LENGTH 256

#define FATAL(...) do {                                                      \
	fprintf (stderr, __VA_ARGS__);                                       \
	exit(EXIT_FAILURE);                                                  \
} while (0)

typedef struct symbol   symbol;
typedef struct skO      skO;
typedef struct skE      skE;
typedef struct context  context;
typedef struct reserved reserved;

struct symbol {
	char   name[SYMBOL_MAX_LENGTH];
	symbol *next;
};

typedef enum {
	SKO_NUMBER,
	SKO_BOOLEAN,
	SKO_CHARACTER,
	SKO_QSYMBOL,
	SKO_SYMBOL,
	SKO_LIST
} skO_t;

struct skO {
	skO   *next;
	skO_t tag;
	union {
		double number;
		int    boolean;
		char   character;
		symbol *sym;
		skO    *list;
	} data;
};

struct skE {
	skO     *stack;
	context *scope;
	int     panic;
	jmp_buf jmp;
};

typedef skO *(skE_natOp)(skE*);

struct context {
	context  *parent;
	reserved *first_def;
};

struct reserved {
	reserved *next;
	symbol   *sym;
	enum {
		KIND_OBJECT,
		KIND_OPERATION,
		KIND_NATIVE
	} kind;
	union {
		skO       *obj;
		skE_natOp *native;
	} data;
};

/*////////////////////////////////////////////////////////////////////////////
//                                 OBJECTS                                  //
////////////////////////////////////////////////////////////////////////////*/

/*
 * Allocate memory for objects and initialize them.
 */
skO *skO_number_new        (double d);
skO *skO_boolean_new       (int b);
skO *skO_character_new     (char c);
skO *skO_quoted_symbol_new (char *a);
skO *skO_symbol_new        (char *a);
skO *skO_list_new          (void);

/*
 * Parse a string.
 */
skO *skO_parse (char **next, jmp_buf jmp, char *delim);

/*
 * Parse a file.
 */
skO *skO_loadParse (char *path);

/*
 * Perform a deep copy of `obj'. Object referenced in the `next' field of the
 * struct is NOT copied but set to `NULL' in the copy.
 */
skO *skO_clone (skO *obj);

/*
 * Release `obj' from memory. If the object is a composite, child objects are
 * also released. Object referenced in the `next' field of the struct is NOT
 * released.
 */
void skO_free (skO *obj);

/*
 * Check if `obj' is tagged with `type'.
 * Halt execution of the program if the check fails.
 */
void skO_checkType (skO *obj, skO_t type);

/*
 * Insert `obj' after the last member of `list'. Can also be used to join two
 * lists.
 */
void sk_list_append (skO *list, skO *obj);

/*////////////////////////////////////////////////////////////////////////////
//                               ENVIRONMENTS                               //
////////////////////////////////////////////////////////////////////////////*/

/* Allocate and initialize a new environment. */
skE *skE_new          (void);
void skE_init         (skE *env);

/* Release an environment and its contents. */
void skE_free         (skE *env);

/* Define or undefine named entities in the current scope. */
void skE_defNative    (skE *env, char *name, skE_natOp *native);
void skE_defObject    (skE *env, skO *sym, skO *obj);
void skE_defOperation (skE *env, skO *sym, skO *obj);
void skE_undef        (skE *env, skO *sym);

/* Go in and out of scope. */
void skE_scopePush    (skE *env);
void skE_scopePop     (skE *env);

/* Push and pop objects to/from the stack. */
void skE_stackPush    (skE *env, skO *obj);
skO *skE_stackPop     (skE *env);

/* Execute objects in an environment. */
void skE_call         (skE *env, skO *sym);
void skE_execList     (skE *env, skO *list, int scoping);

#endif
