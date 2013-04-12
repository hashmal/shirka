/* Copyright (C) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include "shirka.h"

#define SK_INTRINSIC void

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

	skO_checkType(sym, SKO_QSYMBOL);
	skO_checkType(obj, SKO_LIST);

	skE_defOperation(env, sym, obj);
}

SK_INTRINSIC skI_defObject (skE *env)
{
	skO *sym = skE_stackPop(env);
	skO *obj = skE_stackPop(env);

	skE_defObject(env, sym, obj);
}

SK_INTRINSIC skI_undef (skE *env)
{
	skO *sym = skE_stackPop(env);

	skE_undef(env, sym);
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
}

SK_INTRINSIC skI_getc (skE *env)
{
	skE_stackPush(env, skO_character_new(fgetc(stdin)));
}

SK_INTRINSIC skI_true (skE *env)
{
	skE_stackPush(env, skO_boolean_new(1));
}

SK_INTRINSIC skI_false (skE *env)
{
	skE_stackPush(env, skO_boolean_new(0));
}

SK_INTRINSIC skI_add (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);
	l->data.number = l->data.number + r->data.number;
	skO_free(r);
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_sub (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);
	l->data.number = l->data.number - r->data.number;
	skO_free(r);
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_mul (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);
	l->data.number = l->data.number * r->data.number;
	skO_free(r);
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_div (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);
	l->data.number = l->data.number / r->data.number;
	skO_free(r);
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_and (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);
	l->data.boolean = l->data.boolean && r->data.boolean;
	skO_free(r);
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_or (skE *env)
{
	skO *r = skE_stackPop(env);
	skO *l = skE_stackPop(env);
	l->data.boolean = l->data.boolean || r->data.boolean;
	skO_free(r);
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_not (skE *env)
{
	skO *l = skE_stackPop(env);
	l->data.boolean = !l->data.boolean;
	skE_stackPush(env, l);
}

SK_INTRINSIC skI_exec (skE *env)
{
	skO *list = skE_stackPop(env);
	skO_checkType(list, SKO_LIST);

	skE_execList(env, list);
}

SK_INTRINSIC skI_parse (skE *env)
{
	char *cursor;
	char *str;
	skO  *node;
	skO  *ast;
	skO  *list = skE_stackPop(env);
	int  count = 1; /* string will be 0 terminated */
	int  i     = 0;

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
	ast = skO_parse(&cursor);
	free(str);

	skE_stackPush(env, ast);

	skO_free(list);
}

SK_INTRINSIC skI_exec_if (skE *env)
{
	skO *list = skE_stackPop(env);
	skO *b    = skE_stackPop(env);

	skO_checkType(list, SKO_LIST);
	skO_checkType(b, SKO_BOOLEAN);

	if (b->data.boolean) {
		skE_execList(env, list);
	}

	skO_free(b);
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
}

SK_INTRINSIC skI_cons (skE *env)
{
	skO *obj  = skE_stackPop(env);
	skO *list = skE_stackPop(env);

	obj->next = list->data.list;
	list->data.list = obj;

	skE_stackPush(env, list);
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
}

SK_INTRINSIC skI_scopeout (skE *env)
{
	skE_scopePop(env);
}

SK_INTRINSIC skI_scopein (skE *env)
{
	skE_scopePush(env);
}
