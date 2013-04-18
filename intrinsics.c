/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "shirka.h"

#define SK_INTRINSIC skO *

void print_list (skO *list)
{
	skO *node = list->data.list;

	while (node) {
		switch (node->tag) {
		case SKO_QSYMBOL:
		case SKO_SYMBOL:
			printf("%s", (node->data.sym)->name);
			break;
		case SKO_NUMBER:
			printf("%.14g", node->data.number);
			break;
		case SKO_CHARACTER:
			printf("%c", node->data.character);
			break;
		case SKO_LIST:
			print_list(node);
			break;
		default:
			break;
		}

		node = node->next;
	}
}

SK_INTRINSIC skI_defOperation (skE *env)
{
	skO *sym = skE_stackPop(env);
	skO *obj = skE_stackPop(env);

	skE_defOperation(env, sym, obj);

	return NULL;
}

SK_INTRINSIC skI_defObject (skE *env)
{
	skO *sym = skE_stackPop(env);
	skO *obj = skE_stackPop(env);

	skE_defObject(env, sym, obj);

	return NULL;
}

SK_INTRINSIC skI_undef (skE *env)
{
	skO *sym = skE_stackPop(env);

	skE_undef(env, sym);

	return NULL;
}

SK_INTRINSIC skI_print (skE *env)
{
	skO *obj = skE_stackPop(env);

	switch (obj->tag) {
	case SKO_QSYMBOL:
	case SKO_SYMBOL:
		printf("%s", (obj->data.sym)->name);
		break;
	case SKO_NUMBER:
		printf("%.14g", obj->data.number);
		break;
	case SKO_CHARACTER:
		printf("%c", obj->data.character);
		break;
	case SKO_LIST:
		print_list(obj);
		break;
	case SKO_BOOLEAN:
		if (obj->data.boolean) {
			printf("TRUE");
		} else {
			printf("FALSE");
		}
		break;
	default:
		fprintf(stderr, "PANIC! Internal type error.\n");
		env->panic = 1;
		longjmp(env->jmp, 1);
	}

	fflush(stdout);
	skO_free(obj);

	return NULL;
}

SK_INTRINSIC skI_getc (skE *env)
{
	skE_stackPush(env, skO_character_new(fgetc(stdin)));

	return NULL;
}

SK_INTRINSIC skI_true (skE *env)
{
	skE_stackPush(env, skO_boolean_new(1));

	return NULL;
}

SK_INTRINSIC skI_false (skE *env)
{
	skE_stackPush(env, skO_boolean_new(0));

	return NULL;
}

SK_INTRINSIC skI_add (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.number = l->data.number + r->data.number;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_sub (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.number = l->data.number - r->data.number;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_mul (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.number = l->data.number * r->data.number;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_div (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.number = l->data.number / r->data.number;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_pow (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.number = pow(l->data.number, r->data.number);

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_mod (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.number = fmod(l->data.number, r->data.number);

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_abs (skE *env)
{
	skO *l = skE_stackPop(env);

	skO_checkType(l, SKO_NUMBER);

	l->data.number = fabs(l->data.number);

	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_gt (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.boolean = l->data.number > r->data.number;
	l->tag = SKO_BOOLEAN;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_lt (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_NUMBER);
	skO_checkType(l, SKO_NUMBER);

	l->data.boolean = l->data.number < r->data.number;
	l->tag = SKO_BOOLEAN;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_and (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_BOOLEAN);
	skO_checkType(l, SKO_BOOLEAN);

	l->data.boolean = l->data.boolean && r->data.boolean;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_or (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	skO_checkType(r, SKO_BOOLEAN);
	skO_checkType(l, SKO_BOOLEAN);

	l->data.boolean = l->data.boolean || r->data.boolean;

	skO_free(r);
	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_not (skE *env)
{
	skO *l = skE_stackPop(env);

	skO_checkType(l, SKO_BOOLEAN);

	l->data.boolean = !l->data.boolean;

	skE_stackPush(env, l);

	return NULL;
}

SK_INTRINSIC skI_exec (skE *env)
{
	skO *list = skE_stackPop(env);
	skO_checkType(list, SKO_LIST);

	return list;
}

SK_INTRINSIC skI_parse (skE *env)
{
	char    *cursor;
	char    *str;
	skO     *node;
	skO     *list = skE_stackPop(env);
	int     count = 1; /* string will be 0 terminated */
	int     i     = 0;
	jmp_buf jmp;

	skO_checkType(list, SKO_LIST);

	node = list->data.list;
	while (node) {
		count++;
		node = node->next;
	}

	str = malloc(count);

	node = list->data.list;
	while (node) {
		str[i] = node->data.character;
		i++;
		node = node->next;
	}
	str[i] = 0;

	cursor = str;

	if (setjmp(jmp)) {
		free(str);
		skO_free(list);

		longjmp(env->jmp, 1);
	}

	skE_stackPush(env, skO_parse(&cursor, jmp, NULL));

	free(str);
	skO_free(list);

	return NULL;
}

SK_INTRINSIC skI_exec_if (skE *env)
{
	skO *list = skE_stackPop(env);
	skO *b    = skE_stackPop(env);

	skO_checkType(list, SKO_LIST);
	skO_checkType(b, SKO_BOOLEAN);

	if (b->data.boolean) {
		skO_free(b);
	} else {
		skO_free(b);
		skO_free(list);
		list = NULL;
	}

	return list;
}

int skO_eql (skO *l, skO *r)
{
	skO *ln;
	skO *rn;

	if (l->tag == r->tag) {
		switch (l->tag) {
		case SKO_LIST:
			ln = l->data.list;
			rn = r->data.list;

			while (ln && rn) {
				if (!skO_eql(ln, rn))
					return 0;

				ln = ln->next;
				rn = rn->next;
			}

			if (ln != rn) {
				return 0;
			} else {
				return 1;
			}
		default:
			if (l->data.list == r->data.list) {
				return 1;
			} else {
				return 0;
			}
		}
	} else {
		return 0;
	}
}

SK_INTRINSIC skI_eql (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	if (skO_eql(l, r)) {
		skE_stackPush(env, skO_boolean_new(1));
	} else {
		skE_stackPush(env, skO_boolean_new(0));
	}

	skO_free(l);
	skO_free(r);

	return NULL;
}

SK_INTRINSIC skI_length (skE *env)
{
	size_t len = 0;
	skO *list = skE_stackPop(env);
	skO *node = list->data.list;

	while (node) {
		len++;
		node = node->next;
	}

	skE_stackPush(env, list);
	skE_stackPush(env, skO_number_new(len));

	return NULL;
}

SK_INTRINSIC skI_cons (skE *env)
{
	skO *obj  = skE_stackPop(env);
	skO *list = skE_stackPop(env);

	obj->next = list->data.list;
	list->data.list = obj;

	skE_stackPush(env, list);

	return NULL;
}

SK_INTRINSIC skI_uncons (skE *env)
{
	skO *obj;
	skO *list = skE_stackPop(env);

	obj = list->data.list;
	list->data.list = obj->next;
	obj->next = NULL;

	skE_stackPush(env, list);
	skE_stackPush(env, obj);

	return NULL;
}

SK_INTRINSIC skI_with (skE *env)
{
	char buffer[256];
	int i = 0;
	skO *ast;
	skO *skchar;
	skO *fname = skE_stackPop(env);

	skO_checkType(fname, SKO_LIST);
	skchar = fname->data.list;

	while (skchar) {
		buffer[i] = skchar->data.character;
		skchar = skchar->next;
		i++;
	}
	buffer[i] = 0;

	ast = skO_loadParse(buffer);
	skO_free(fname);

	skE_execList(env, ast, 0);

	return NULL;
}

SK_INTRINSIC skI_type (skE *env)
{
	skO *obj = skE_stackPop(env);
	skO *sym;

	switch (obj->tag) {
	case SKO_QSYMBOL:
		sym = skO_symbol_new("QuotedSymbol");
		break;
	case SKO_SYMBOL:
		sym = skO_symbol_new("Symbol");
		break;
	case SKO_NUMBER:
		sym = skO_symbol_new("Number");
		break;
	case SKO_CHARACTER:
		sym = skO_symbol_new("Character");
		break;
	case SKO_LIST:
		sym = skO_symbol_new("List");
		break;
	case SKO_BOOLEAN:
		sym = skO_symbol_new("Boolean");
		break;
	default:
		fprintf(stderr, "PANIC! Internal type error: %i\n", obj->tag);
		env->panic = 1;
		longjmp(env->jmp, 1);
	}

	skE_stackPush(env, obj);
	sym->tag = SKO_QSYMBOL;
	skE_stackPush(env, sym);

	return NULL;
}

SK_INTRINSIC skI_quote (skE *env)
{
	skO *sym = skE_stackPop(env);
	sym->tag = SKO_QSYMBOL;
	skE_stackPush(env, sym);

	return NULL;
}

SK_INTRINSIC skI_unquote (skE *env)
{
	skO *sym = skE_stackPop(env);
	sym->tag = SKO_SYMBOL;
	skE_stackPush(env, sym);

	return NULL;
}

SK_INTRINSIC skI_try (skE *env)
{
	skO *action = skE_stackPop(env);
	skE *local  = skE_new();
	skO *result;

	skO_checkType(action, SKO_LIST);

	local->scope = env->scope;
	local->stack = NULL;

	if (setjmp(local->jmp)) {
		skE_stackPush(env, skO_quoted_symbol_new("$try/failed"));
	} else {
		skE_execList(local, action, 1);
		result = skO_list_new();
		result->data.list = local->stack;
		local->stack = NULL;
		skE_stackPush(env, result);
		skE_stackPush(env, skO_quoted_symbol_new("$try/ok"));
	}

	local->scope = NULL;
	skE_scopePush(local);
	skE_free(local);
	return NULL;
}
