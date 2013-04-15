/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include "shirka.h"

void load_intrinsics (skE *env);

skO *skE_stackPop (skE *env)
{
	skO *obj;

	if (env->stack == NULL) {
		printf("PANIC! Tried to pop object but stack is empty.\n");
		env->panic = 1;
		exit(EXIT_FAILURE);
	}
	obj = env->stack;
	env->stack = obj->next;
	obj->next = NULL;
	return obj;
}

void skE_stackPush (skE *env, skO *obj)
{
	obj->next = env->stack;	
	env->stack = obj;
}

context *scope_get (skE *env)
{
	return env->scope;
}

reserved *scope_find (skE *env, skO *sym)
{
	context *current_scope = env->scope;
	reserved *node;
	reserved *next;

	skO_checkType(sym, SKO_SYMBOL);

	while (current_scope != NULL) {
		node = current_scope->first_def;
		while (node != NULL) {
			next = node->next;
			if (node->sym == sym->data.sym) {
				return node;
			}
			node = next;
		}
		current_scope = current_scope->parent;
	}

	return NULL;
}

reserved *scope_find_current (skE *env, skO *sym)
{
	reserved *node;
	reserved *next;

	node = env->scope->first_def;
	while (node != NULL) {
		next = node->next;
		if (node->sym == sym->data.sym) {
			return node;
		}
		node = next;
	}

	return NULL;
}

skE *skE_new (void)
{
	skE *env = (skE *)malloc(sizeof(skE));
	env->stack = NULL;
	env->scope = NULL;
	env->panic = 0;
	skE_scopePush(env);
	load_intrinsics(env);
	return env;
}

void skE_free (skE *env)
{
	if (env->stack != NULL)
		printf("WARNING! Stack non empty upon exit.\n");

	skE_scopePop(env);
	free(env);
}

void skE_defNative (skE *env, char *name, skE_natOp *native)
{
	reserved *slot;
	skO *obj = skO_symbol_new(name);

	slot              = (reserved *)malloc(sizeof(reserved));
	slot->next        = env->scope->first_def;
	slot->sym         = obj->data.sym;
	slot->kind        = KIND_NATIVE;
	slot->data.native = native;

	env->scope->first_def = slot;

	skO_free(obj);
}

void skE_defObject (skE *env, skO *sym, skO *obj)
{
	reserved *slot;

	skO_checkType(sym, SKO_QSYMBOL);

	if (scope_find_current(env, sym) != NULL) {
		/* Release previously defined object? */
	}

	slot           = (reserved *)malloc(sizeof(reserved));
	slot->next     = scope_get(env)->first_def;
	slot->sym      = sym->data.sym;
	slot->kind     = KIND_OBJECT;
	slot->data.obj = obj;

	scope_get(env)->first_def = slot;

	skO_free(sym);
}

void skE_defOperation (skE *env, skO *sym, skO *obj)
{
	reserved *slot;

	skO_checkType(sym, SKO_QSYMBOL);
	skO_checkType(obj, SKO_LIST);

	if (scope_find_current(env, sym) != NULL) {
		printf("PANIC! Can't redefine reserved operation %s.\n", sym->data.sym->name);
		env->panic = 1;
		exit(EXIT_FAILURE);
	}

	slot           = (reserved *)malloc(sizeof(reserved));
	slot->next     = scope_get(env)->first_def;
	slot->sym      = sym->data.sym;
	slot->kind     = KIND_OPERATION;
	slot->data.obj = obj;

	scope_get(env)->first_def = slot;

	skO_free(sym);
}

void skE_undef (skE *env, skO *sym)
{
	reserved *r;
	reserved *node = scope_get(env)->first_def;

	skO_checkType(sym, SKO_QSYMBOL);

	r = scope_find_current(env, sym);

	if (r == NULL) {
		printf("PANIC! Reserved object not found in current scope.\n");
		env->panic = 1;
		exit(EXIT_FAILURE);
	}

	if (r->kind != KIND_OBJECT) {
		printf("PANIC! Expected object, got native or operation.\n");
		env->panic = 1;
		exit(EXIT_FAILURE);
	}

	if (node == r) {
		scope_get(env)->first_def = r->next;
		
	} else {
		while (node->next != r)
			node = node->next;

		node->next = r->next;
	}

	r->next = NULL;
	skE_stackPush(env, r->data.obj);
	free(r);
	skO_free(sym);
}

void skE_scopePush (skE *env)
{
	context *ct = (context *)malloc(sizeof(context));
	ct->parent = env->scope;
	ct->first_def = NULL;
	env->scope = ct;
}

#ifdef SK_DEBUG_SCOPE
void p_scope_data (skE *env)
{
	reserved *r = env->scope->first_def;
	printf("[");
	if (r != NULL) {
		printf("%s", r->sym->name);
		r = r->next;
	}
	while (r != NULL) {
		printf(" %s", r->sym->name);
		r = r->next;
	}
	printf("]");
}
#endif

void skE_scopePop (skE *env)
{
	reserved *node;
	reserved *next;
	context *expired = env->scope;

	#ifdef SK_DEBUG_SCOPE
	printf("    (undef) ");
	p_scope_data(env);
	printf("\n");
	#endif

	env->scope = expired->parent;

	node = expired->first_def;
	while (node != NULL) {
		next = node->next;
		if (node->kind == KIND_OBJECT || node->kind == KIND_OPERATION) {
			skO_free(node->data.obj);
		}
		free(node);
		node = next;
	}

	free(expired);
}

void skE_call (skE *env, skO *sym)
{
	skO *list = skO_list_new();
	sk_list_append(list, sym);

	skE_execList(env, list, 1);

	skO_free(list);
}

void skE_execList (skE *env, skO *list, int scoping)
{
	skO *cont;
	skO *head = list->data.list;
	skO *tok;
	reserved *r;

	list->data.list = NULL;
	skO_free(list);

	if (scoping)
		skE_scopePush(env);

#ifdef SK_O_TAIL
tail:
#endif
	while (head != NULL) {
		tok = head;
		head = head->next;

		switch (tok->tag) {
		case SKO_SYMBOL:
			r = scope_find(env, tok);
			if (r == NULL) {
				printf("PANIC! Not found: %s\n", (tok->data.sym)->name);
				env->panic = 1;
				exit(EXIT_FAILURE);
			}
			switch (r->kind) {
			case KIND_OBJECT:
				skE_stackPush(env, skO_clone(r->data.obj));
				break;
			case KIND_OPERATION:
				#ifdef SK_O_TAIL
				if (tok->next == NULL) {
					skO_free(tok);
					tok = skO_clone(r->data.obj);
					head = tok->data.list;
					tok->data.list = NULL;
					skO_free(tok);
					goto tail;
				} else {
					skE_execList(env, skO_clone(r->data.obj), 1);
				}
				#else
				skE_execList(env, skO_clone(r->data.obj), 1);
				#endif
				break;
			case KIND_NATIVE:
				#ifdef SK_O_TAIL
				cont = r->data.native(env);

				if (tok->next == NULL) {

					if (cont != NULL) {
						skO_free(tok);
						head = cont->data.list;
						cont->data.list = NULL;
						skO_free(cont);
						goto tail;
					}
				} else {
					if (cont != NULL)
						skE_execList(env, cont, 1);
				}
				#else
				cont = r->data.native(env);
				if (cont != NULL)
					skE_execList(env, cont, 1);
				#endif
				break;
			default:
				printf("PANIC! Internal kind error.\n");
				break;
			}

			skO_free(tok);
			break;
		case SKO_QSYMBOL:
		case SKO_NUMBER:
		case SKO_CHARACTER:
		case SKO_LIST:
		case SKO_BOOLEAN:
			skE_stackPush(env, tok);
			break;
		default:
			printf("PANIC! Internal type error.\n");
			env->panic = 1;
			exit(EXIT_FAILURE);
		}
	}

	if (scoping)
		skE_scopePop(env);
}

skO *skO_loadParse (char *path)
{
	FILE   *f;
	size_t f_size;
	char   *src;
	char   *cursor;
	skO    *ast;
	jmp_buf jmp;

	if (setjmp(jmp)) {
		printf("Syntax error in file %s\n", path);
		exit(EXIT_FAILURE);
	}

	f = fopen(path, "rb");
	if (f == NULL) {
		printf("INTERPRETER ERROR! Could not open file.\n");
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
	ast = skO_parse(&cursor, jmp, NULL);

	free(src);

	return ast;
}

#include "intrinsics.c"

void load_intrinsics (skE *env)
{
	/* Meta */
	skE_defNative(env, "!",         &skI_exec);
	skE_defNative(env, "!?",        &skI_exec_if);
	skE_defNative(env, "=",         &skI_eql);
	skE_defNative(env, "$parse",    &skI_parse);
	skE_defNative(env, "with",      &skI_with);
	skE_defNative(env, "type?",     &skI_type);
	/* Symbol operations */
	skE_defNative(env, "quote",     &skI_quote);
	skE_defNative(env, "unquote",   &skI_unquote);
	/* List operations */
	skE_defNative(env, "length",    &skI_length);
	skE_defNative(env, "cons",      &skI_cons);
	skE_defNative(env, "uncons",    &skI_uncons);
	/* Reserving operations */
	skE_defNative(env, "$=>",       &skI_defOperation);
	skE_defNative(env, "$->",       &skI_defObject);
	skE_defNative(env, "$<-",       &skI_undef);
	/* Boolean data */
	skE_defNative(env, "TRUE",      &skI_true);
	skE_defNative(env, "FALSE",     &skI_false);
	/* Boolean operations */
	skE_defNative(env, "and",       &skI_and);
	skE_defNative(env, "or",        &skI_or);
	skE_defNative(env, "not",       &skI_not);
	/* Math operations */
	skE_defNative(env, "+",         &skI_add);
	skE_defNative(env, "-",         &skI_sub);
	skE_defNative(env, "*",         &skI_mul);
	skE_defNative(env, "/",         &skI_div);
	skE_defNative(env, "^",         &skI_pow);
	skE_defNative(env, "abs",       &skI_abs);
	skE_defNative(env, ">",         &skI_gt);
	skE_defNative(env, "<",         &skI_lt);
	/* IO operations */
	skE_defNative(env, "print",     &skI_print);
	skE_defNative(env, "getc",      &skI_getc);
}
