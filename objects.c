/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shirka.h"

symbol *symbol_list = NULL;

symbol *symbol_id_from_string (char *str)
{
	symbol *sym = symbol_list;

	while (sym) {
		if (strcmp(str, sym->name) == 0) {
			goto symbol_found;
		}

		sym = sym->next;
	}

	sym = (symbol *)malloc(sizeof(symbol));
	strcpy(sym->name, str);
	sym->next = symbol_list;

	symbol_list = sym;

symbol_found:
	return sym;
}

skO *skO_clone (skO *obj)
{
	skO *copy;
	skO *iter;
	skO *child_copy;

	copy       = (skO *)malloc(sizeof(skO));
	copy->next = NULL;
	copy->tag  = obj->tag;

	switch (obj->tag) {
	case SKO_SYMBOL:
	case SKO_QSYMBOL:
	case SKO_NUMBER:
	case SKO_BOOLEAN:
	case SKO_CHARACTER:
		copy->data = obj->data;
		break;
	case SKO_LIST:
		copy->data.list = NULL;

		iter = obj->data.list;
		while (iter) {
			child_copy = skO_clone(iter);
			sk_list_append(copy, child_copy);
			iter = iter->next;
		}

		break;
	default:
		fprintf(stderr, "Internal type error.\n");
		exit(EXIT_FAILURE);
	}

	return copy;
}

void skO_free (skO *obj)
{
	skO *node;
	skO *next;

	switch (obj->tag) {
	case SKO_LIST:
		node = obj->data.list;
		while (node) {
			next = node->next;

			skO_free(node);
			node = next;
		}
	case SKO_SYMBOL:
	case SKO_QSYMBOL:
	case SKO_NUMBER:
	case SKO_BOOLEAN:
	case SKO_CHARACTER:
		free (obj);
		break;
	default:
		fprintf(stderr, "Internal type error.\n");
		exit(EXIT_FAILURE);
		break;
	}
}

skO *skO_number_new (double d)
{
	skO *obj = (skO *)malloc(sizeof(skO));

	obj->next        = NULL;
	obj->tag         = SKO_NUMBER;
	obj->data.number = d;

	return obj;
}

skO *skO_boolean_new (int b)
{
	skO *obj = (skO *)malloc(sizeof(skO));

	obj->next         = NULL;
	obj->tag          = SKO_BOOLEAN;
	obj->data.boolean = b;

	return obj;
}

skO *skO_character_new (char c)
{
	skO *obj = (skO *)malloc(sizeof(skO));

	obj->next           = NULL;
	obj->tag            = SKO_CHARACTER;
	obj->data.character = c;

	return obj;
}

skO *skO_quoted_symbol_new (char *a)
{
	skO *obj = (skO *)malloc(sizeof(skO));

	obj->next     = NULL;
	obj->tag      = SKO_QSYMBOL;
	obj->data.sym = symbol_id_from_string(a);

	return obj;
}

skO *skO_symbol_new (char *a)
{
	skO *obj = (skO *)malloc(sizeof(skO));

	obj->next     = NULL;
	obj->tag      = SKO_SYMBOL;
	obj->data.sym = symbol_id_from_string(a);

	return obj;
}

skO *skO_list_new (void)
{
	skO *obj = (skO *)malloc(sizeof(skO));

	obj->next      = NULL;
	obj->tag       = SKO_LIST;
	obj->data.list = NULL;

	return obj;
}

void sk_list_append (skO *list, skO *obj)
{
	skO *node;

	skO_checkType(list, SKO_LIST);

	node = list->data.list;

	if (node) {
		while (node->next) {
			node = node->next;
		}
		node->next = obj;
	} else {
		list->data.list = obj;
	}
}

const char *NUMBER_AS_STRING    = "Number";
const char *BOOLEAN_AS_STRING   = "Boolean";
const char *CHARACTER_AS_STRING = "Character";
const char *QSYMBOL_AS_STRING   = "QuotedSymbol";
const char *SYMBOL_AS_STRING    = "Symbol";
const char *LIST_AS_STRING      = "List";

const char *tystr (size_t i)
{
	switch (i) {
	case SKO_NUMBER:    return NUMBER_AS_STRING;
	case SKO_BOOLEAN:   return BOOLEAN_AS_STRING;
	case SKO_CHARACTER: return CHARACTER_AS_STRING;
	case SKO_QSYMBOL:   return QSYMBOL_AS_STRING;
	case SKO_SYMBOL:    return SYMBOL_AS_STRING;
	case SKO_LIST:      return LIST_AS_STRING;
	default:
		fprintf(stderr, "Internal type error.\n");
		exit(EXIT_FAILURE);
	}
}

void skO_checkType(skO *obj, skO_t type)
{
	if (obj->tag == type)
		return;

	fprintf(stderr, "INTERPRETER ERROR! Expected `%s' but got `%s'.\n",
		tystr(type), tystr(obj->tag));
	exit(EXIT_FAILURE);
}
