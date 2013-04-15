/* Copyright (C) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "shirka.h"

#define SK_INTRINSIC skO *

void print_list (skO *list)
{
	skO *node = list->data.list;

	while (node != NULL) {
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
		printf("PANIC! Internal type error.\n");
		exit(EXIT_FAILURE);
		break;
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
	while (node != NULL) {
		count++;
		node = node->next;
	}

	str = (char *)malloc(count);

	node = list->data.list;
	while (node != NULL) {
		str[i] = node->data.character;
		i++;
		node = node->next;
	}
	str[i] = 0;

	cursor = str;

	if (setjmp(jmp)) {
		skE_stackPush(env, skO_list_new());
		skE_stackPush(env, skO_quoted_symbol_new("$parse/failed"));

		free(str);
		skO_free(list);

		return NULL;
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

SK_INTRINSIC skI_eql (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);

	if (l->tag == r->tag) {
		switch (l->tag) {
		case SKO_LIST:
			printf("PANIC! Equality testing not yet implemented for lists.\n");
			exit(EXIT_FAILURE);
		default:
			if (l->data.list == r->data.list) {
				skE_stackPush(env, skO_boolean_new(1));
			} else {
				skE_stackPush(env, skO_boolean_new(0));
			}
		}
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

	while (node != NULL) {
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

	while (skchar != NULL) {
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
		printf("PANIC! Internal type error");
		exit(EXIT_FAILURE);
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
