/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include "shirka.h"

void load_intrinsics (skE *env);

/* Pending instructions and the detached token currently being executed remain
 * owned by the environment, even if a native operation jumps out of execution. */
struct execution {
	execution *parent; /* Enclosing execution in this environment. */
	skO *list;         /* Remaining instructions, excluding current. */
	skO *current;      /* Detached token being processed; NULL once released. */
};

void skE_error (skE *env, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	fprintf(stderr, "PANIC! ");
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	va_end(args);
	longjmp(env->jmp, 1);
}

void skE_checkType (skE *env, skO *obj, skO_t type)
{
	if (obj->tag != type)
		skE_error(env, "Expected `%s' but got `%s'.",
			skO_typeName(type), skO_typeName(obj->tag));
}

skO *skE_stackPeek (skE *env, size_t depth)
{
	skO *obj = env->stack;

	while (obj && depth) {
		obj = obj->next;
		depth--;
	}
	if (!obj)
		skE_error(env, "Stack underflow.");

	return obj;
}

skO *skE_stackPop (skE *env)
{
	skO *obj = skE_stackPeek(env, 0);

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

	while (current_scope) {
		node = current_scope->first_def;
		while (node) {
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
	while (node) {
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
	skE *env = malloc(sizeof(skE));
	env->stack = NULL;
	env->scope = NULL;
	env->scope_base = NULL;
	env->execution = NULL;

	return env;
}

void skE_init (skE *env)
{
	skE_scopePush(env);
	load_intrinsics(env);
}

void skE_free (skE *env)
{
	execution *frame;

	while (env->execution) {
		frame = env->execution;
		env->execution = frame->parent;
		if (frame->current)
			skO_free(frame->current);
		skO_free(frame->list);
		free(frame);
	}
	while (env->stack)
		skO_free(skE_stackPop(env));
	while (env->scope != env->scope_base)
		skE_scopePop(env);
	free(env);
}

void skE_defNative (skE *env, char *name, skE_natOp *native)
{
	reserved *slot;
	skO *obj = skO_symbol_new(name);

	slot              = malloc(sizeof(reserved));
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

	skE_checkType(env, sym, SKO_QSYMBOL);

	if (scope_find_current(env, sym)) {
		/* Release previously defined object? */
	}

	slot           = malloc(sizeof(reserved));
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

	skE_checkType(env, sym, SKO_QSYMBOL);
	skE_checkType(env, obj, SKO_LIST);

	if (scope_find_current(env, sym)) {
		fprintf(stderr, "PANIC! Can't redefine reserved operation %s.\n", sym->data.sym->name);
		skO_free(sym);
		skO_free(obj);
		longjmp(env->jmp, 1);
	}

	slot           = malloc(sizeof(reserved));
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

	skE_checkType(env, sym, SKO_QSYMBOL);

	r = scope_find_current(env, sym);

	if (!r) {
		fprintf(stderr, "PANIC! Reserved object not found in current scope.\n");
		skO_free(sym);
		longjmp(env->jmp, 1);
	}

	if (r->kind != KIND_OBJECT) {
		fprintf(stderr, "PANIC! Expected object, got native or operation.\n");
		skO_free(sym);
		longjmp(env->jmp, 1);
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
	context *ct = malloc(sizeof(context));
	ct->parent = env->scope;
	ct->first_def = NULL;
	env->scope = ct;
}

#ifdef SK_DEBUG_SCOPE
void p_scope_data (skE *env)
{
	reserved *r = env->scope->first_def;
	printf("[");
	if (r) {
		printf("%s", r->sym->name);
		r = r->next;
	}
	while (r) {
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
	while (node) {
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
}

void skE_execList (skE *env, skO *list, int scoping)
{
	execution *frame = malloc(sizeof(execution));
	skO *cont;
	skO *tok;
	reserved *r;

	frame->parent = env->execution;
	frame->list = list;
	frame->current = NULL;
	env->execution = frame;

	if (scoping)
		skE_scopePush(env);

	while (frame->list->data.list) {
		tok = frame->list->data.list;
		frame->list->data.list = tok->next;
		tok->next = NULL;
		frame->current = tok;
		cont = NULL;

		switch (tok->tag) {
		case SKO_SYMBOL:
			r = scope_find(env, tok);
			if (!r)
				skE_error(env, "Not found: %s", tok->data.sym->name);
			switch (r->kind) {
			case KIND_OBJECT:
				skE_stackPush(env, skO_clone(r->data.obj));
				break;
			case KIND_OPERATION:
				cont = skO_clone(r->data.obj);
				break;
			case KIND_NATIVE:
				cont = r->data.native(env);
				break;
			default:
				skE_error(env, "Internal kind error.");
			}
			skO_free(tok);
			frame->current = NULL;
			break;
		case SKO_QSYMBOL:
		case SKO_NUMBER:
		case SKO_CHARACTER:
		case SKO_LIST:
		case SKO_BOOLEAN:
			skE_stackPush(env, tok);
			frame->current = NULL;
			break;
		default:
			skE_error(env, "Internal type error.");
		}

		if (cont) {
			#ifdef SK_O_TAIL
			if (!frame->list->data.list) {
				skO_free(frame->list);
				frame->list = cont;
				continue;
			}
			#endif
			skE_execList(env, cont, 1);
		}
	}

	if (scoping)
		skE_scopePop(env);
	env->execution = frame->parent;
	skO_free(frame->list);
	free(frame);
}

skO *skO_loadParse (skE *env, char *path)
{
	FILE   *f;
	long   file_size_result;
	size_t source_length;
	char   *src;
	char   *cursor;
	skO    *ast;
	jmp_buf jmp;

	f = fopen(path, "rb");
	if (!f)
		skE_error(env, "Could not open file %s.", path);
	/* get file size */
	if (fseek(f, 0, SEEK_END) != 0)
		goto read_error;
	file_size_result = ftell(f);
	if (file_size_result < 0 || (uintmax_t)file_size_result >= (uintmax_t)SIZE_MAX)
		goto read_error;
	/* ftell returns a signed result; convert only after checking it. */
	source_length = (size_t)file_size_result;
	if (fseek(f, 0, SEEK_SET) != 0)
		goto read_error;
	/* copy source into string */
	src = malloc(source_length + 1);
	if (!src)
		goto read_error;
	if (fread(src, 1, source_length, f) != source_length || ferror(f)) {
		free(src);
		goto read_error;
	}
	src[source_length] = 0;
	fclose(f);

	if (setjmp(jmp)) {
		free(src);
		skE_error(env, "Syntax error in file %s.", path);
	}

	cursor = src;
	ast = skO_parse(&cursor, jmp, NULL);

	free(src);

	return ast;

read_error:
	fclose(f);
	skE_error(env, "Could not read file %s.", path);
	return NULL; /* skE_error does not return. */
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
	skE_defNative(env, "try",     &skI_try);
	/* Symbol operations */
	skE_defNative(env, "quote",     &skI_quote);
	skE_defNative(env, "unquote",   &skI_unquote);
	/* List operations */
	skE_defNative(env, "length?",   &skI_length);
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
	skE_defNative(env, "%",         &skI_mod);
	skE_defNative(env, "abs",       &skI_abs);
	skE_defNative(env, ">",         &skI_gt);
	skE_defNative(env, "<",         &skI_lt);
	/* IO operations */
	skE_defNative(env, "print",     &skI_print);
	skE_defNative(env, "getc",      &skI_getc);
}
