/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include "hashmap.h"
#include <string.h>

void load_intrinsics (skE *env);

/* Pending instructions and the detached token currently being executed remain
 * owned by the environment, even if a native operation jumps out of execution. */
struct execution {
	execution *parent; /* Enclosing execution in this environment. */
	skO *list;         /* Remaining instructions, excluding current. */
	int scoped;       /* This frame owns an invocation scope. */
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

static symbol *part (const char *name, size_t length)
{
	char buffer[SYMBOL_MAX_LENGTH];
	skO *object;
	symbol *key;

	memcpy(buffer, name, length);
	buffer[length] = 0;
	object = skO_symbol_new(buffer);
	key = object->data.sym;
	skO_free(object);
	return key;
}

static const char *path_dot (const char *name)
{
	return strchr(name, '.');
}

static const skH_binding *find (skL *scope, symbol *key)
{
	const char *name = key->name;
	const char *dot;
	const skH_binding *binding;

	while ((dot = path_dot(name))) {
		binding = skH_get(scope->map, part(name, (size_t)(dot - name)));
		if (!binding || !binding->value || binding->value->tag != SKO_LIST || !binding->value->scope)
			return NULL;
		scope = binding->value->scope;
		name = dot + 1;
	}
	binding = skH_get(scope->map, name == key->name ? key : part(name, strlen(name)));
	return binding && binding->kind != SKH_HIDDEN ? binding : NULL;
}

skE *skE_new (void)
{
	skE *env = malloc(sizeof(skE));
	env->stack = NULL;
	env->scope = NULL;
	env->scope_base = NULL;
	env->execution = NULL;
	env->interrupt = NULL;

	return env;
}

void skE_init (skE *env)
{
	skE_scopePush(env);
	load_intrinsics(env);
}

void skE_unwind (skE *env, context *stop)
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
	while (env->scope != stop)
		skE_scopePop(env);
}

void skE_free (skE *env)
{
	skE_unwind(env, env->scope_base);
	while (env->stack)
		skO_free(skE_stackPop(env));
	free(env);
}

void skE_defNative (skE *env, char *name, skE_natOp *native)
{
	skO *key = skO_symbol_new(name);

	skL_write(&env->scope->bindings);
	if (!skH_setNative(env->scope->bindings->map, key->data.sym, native))
		FATAL("PANIC! Could not define native operation.\n");
	skO_free(key);
}

/* Work on an independent root value before publishing a dotted update. */
static int store (skH *map, symbol *key, skO *value, skH_kind kind)
{
	return kind == SKH_HIDDEN ? skH_hide(map, key) : skH_set(map, key, value, kind);
}

static int assign (skE *env, symbol *key, skO *value, skH_kind kind)
{
	const char *name = key->name;
	const char *dot = path_dot(name);
	const skH_binding *binding;
	skO *root;
	skO *target;
	skL **scope;
	symbol *root_key;
	skH_kind root_kind;

	if (!dot) {
		skL_write(&env->scope->bindings);
		return store(env->scope->bindings->map, key, value, kind);
	}
	root_key = part(name, (size_t)(dot - name));
	binding = skH_get(env->scope->bindings->map, root_key);
	if (!binding || !binding->value || binding->value->tag != SKO_LIST)
		return 0;
	root = skO_clone(binding->value);
	root_kind = binding->kind;
	target = root;
	name = dot + 1;
	for (;;) {
		if (!target->scope)
			target->scope = skL_new(NULL);
		scope = &target->scope;
		skL_write(scope);
		dot = path_dot(name);
		key = part(name, dot ? (size_t)(dot - name) : strlen(name));
		if (!dot) {
			if (!store((*scope)->map, key, value, kind))
				FATAL("PANIC! Could not assign field.\n");
			break;
		}
		binding = skH_get((*scope)->map, key);
		if (!binding || !binding->value || binding->value->tag != SKO_LIST) {
			skO_free(root);
			return 0;
		}
		target = skO_clone(binding->value);
		if (!skH_set((*scope)->map, key, target, binding->kind))
			FATAL("PANIC! Could not copy field.\n");
		name = dot + 1;
	}
	skL_write(&env->scope->bindings);
	if (!skH_set(env->scope->bindings->map, root_key, root, root_kind))
		FATAL("PANIC! Could not assign record.\n");
	return 1;
}

void skE_defObject (skE *env, skO *sym, skO *obj)
{
	int success;

	skE_checkType(env, sym, SKO_QSYMBOL);
	if (obj->tag == SKO_LIST && !obj->scope)
		skO_capture(obj, env->scope->bindings);
	success = assign(env, sym->data.sym, obj, SKH_VALUE);
	skO_free(sym);
	if (!success) {
		skO_free(obj);
		skE_error(env, "Invalid binding path.");
	}
}

void skE_defOperation (skE *env, skO *sym, skO *obj)
{
	const char *name;
	int success;

	skE_checkType(env, sym, SKO_QSYMBOL);
	skE_checkType(env, obj, SKO_LIST);
	if (!obj->scope)
		skO_capture(obj, env->scope->bindings);
	name = path_dot(sym->data.sym->name) ? strrchr(sym->data.sym->name, '.') : NULL;
	obj->self = name ? part(name + 1, strlen(name + 1)) : sym->data.sym;
	success = assign(env, sym->data.sym, obj, SKH_OPERATION);
	skO_free(sym);
	if (!success) {
		skO_free(obj);
		skE_error(env, "Invalid operation path.");
	}
}

void skE_undef (skE *env, skO *sym)
{
	const skH_binding *binding;
	skO *value;

	skE_checkType(env, sym, SKO_QSYMBOL);
	binding = find(env->scope->bindings, sym->data.sym);
	if (!binding || binding->kind != SKH_VALUE) {
		skO_free(sym);
		skE_error(env, "Restore requires a value binding in the current lexical scope.");
	}
	if (path_dot(sym->data.sym->name)) {
		value = skO_clone(binding->value);
		if (!assign(env, sym->data.sym, NULL, SKH_HIDDEN))
			FATAL("PANIC! Could not restore field.\n");
	} else {
		skL_write(&env->scope->bindings);
		value = skH_take(env->scope->bindings->map, sym->data.sym, NULL);
		if (!value) {
			binding = find(env->scope->bindings, sym->data.sym);
			value = skO_clone(binding->value);
		}
		if (!skH_hide(env->scope->bindings->map, sym->data.sym))
			FATAL("PANIC! Could not hide restored binding.\n");
	}
	skE_stackPush(env, value);
	skO_free(sym);
}

void skE_scopePush (skE *env)
{
	context *scope = malloc(sizeof(context));

	if (!scope)
		FATAL("PANIC! Could not allocate invocation.\n");
	scope->parent = env->scope;
	scope->bindings = skL_new(env->scope ? env->scope->bindings : NULL);
	env->scope = scope;
}

void skE_scopePop (skE *env)
{
	context *scope = env->scope;

	env->scope = scope->parent;
	skL_free(scope->bindings);
	free(scope);
}

static void invocation (skE *env, skO *list)
{
	context *scope;
	skO *self;

	skE_scopePush(env);
	scope = env->scope;
	skL_free(scope->bindings);
	scope->bindings = skL_new(list->scope);
	if (list->self) {
		self = skO_clone(list);
		if (!skH_set(scope->bindings->map, list->self, self, SKH_OPERATION))
			FATAL("PANIC! Could not install recursive operation.\n");
	}
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
	const skH_binding *r;

	frame->parent = env->execution;
	frame->list = list;
	frame->current = NULL;
	env->execution = frame;

	frame->scoped = scoping;
	if (!list->scope)
		skO_capture(list, env->scope->bindings);
	if (scoping)
		invocation(env, list);

	while (frame->list->data.list) {
		if (env->interrupt && *env->interrupt)
			skE_error(env, "Interrupted.");
		tok = frame->list->data.list;
		frame->list->data.list = tok->next;
		tok->next = NULL;
		frame->current = tok;
		cont = NULL;

		switch (tok->tag) {
		case SKO_SYMBOL:
			r = find(env->scope->bindings, tok->data.sym);
			if (!r)
				skE_error(env, "Not found: %s", tok->data.sym->name);
			switch (r->kind) {
			case SKH_VALUE:
				skE_stackPush(env, skO_clone(r->value));
				break;
			case SKH_OPERATION:
				cont = skO_clone(r->value);
				break;
			case SKH_NATIVE:
				cont = r->native(env);
				break;
			default:
				skE_error(env, "Internal kind error.");
			}
			skO_free(tok);
			frame->current = NULL;
			break;
		case SKO_LIST:
			if (tok->literal)
				skO_capture(tok, env->scope->bindings);
			/* fall through */
		case SKO_QSYMBOL:
		case SKO_NUMBER:
		case SKO_CHARACTER:
		case SKO_BOOLEAN:
			skE_stackPush(env, tok);
			frame->current = NULL;
			break;
		default:
			skE_error(env, "Internal type error.");
		}

		if (cont) {
			if (!cont->scope)
				skO_capture(cont, env->scope->bindings);
			#ifdef SK_O_TAIL
			if (!frame->list->data.list) {
				skO_free(frame->list);
				frame->list = cont;
				if (frame->scoped)
					skE_scopePop(env);
				invocation(env, cont);
				frame->scoped = 1;
				continue;
			}
			#endif
			skE_execList(env, cont, 1);
		}
	}

	if (frame->scoped)
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
	skE_defNative(env, "abort",     &skI_abort);
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
