/* Copyright (c) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "shirka.h"

symbol *symbol_list = NULL;

symbol *symbol_id_from_string (char *str)
{
	symbol *sym = symbol_list;

	if (strlen(str) >= SYMBOL_MAX_LENGTH)
		FATAL("PANIC! Symbol too long.\n");

	while (sym) {
		if (strcmp(str, sym->name) == 0) {
			goto symbol_found;
		}

		sym = sym->next;
	}

	sym = malloc(sizeof(symbol));
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

	copy       = malloc(sizeof(skO));
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
	skO *obj = malloc(sizeof(skO));

	obj->next        = NULL;
	obj->tag         = SKO_NUMBER;
	obj->data.number = d;

	return obj;
}

skO *skO_boolean_new (int b)
{
	skO *obj = malloc(sizeof(skO));

	obj->next         = NULL;
	obj->tag          = SKO_BOOLEAN;
	obj->data.boolean = b;

	return obj;
}

skO *skO_character_new (char c)
{
	skO *obj = malloc(sizeof(skO));

	obj->next           = NULL;
	obj->tag            = SKO_CHARACTER;
	obj->data.character = c;

	return obj;
}

skO *skO_quoted_symbol_new (char *a)
{
	skO *obj = malloc(sizeof(skO));

	obj->next     = NULL;
	obj->tag      = SKO_QSYMBOL;
	obj->data.sym = symbol_id_from_string(a);

	return obj;
}

skO *skO_symbol_new (char *a)
{
	skO *obj = malloc(sizeof(skO));

	obj->next     = NULL;
	obj->tag      = SKO_SYMBOL;
	obj->data.sym = symbol_id_from_string(a);

	return obj;
}

skO *skO_list_new (void)
{
	skO *obj = malloc(sizeof(skO));

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

const char *skO_typeName (skO_t type)
{
	switch (type) {
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
		skO_typeName(type), skO_typeName(obj->tag));
	exit(EXIT_FAILURE);
}

/* Inspection never follows more data than fits in the caller's buffer. */
struct inspection {
	char *buffer;
	size_t size;
	size_t used;
	int shortened;
};

static void inspect_text (struct inspection *out, const char *text)
{
	while (*text && out->used + 1 < out->size)
		out->buffer[out->used++] = *text++;
	if (*text)
		out->shortened = 1;
}

static void inspect_byte (struct inspection *out, unsigned char c)
{
	char escaped[8];

	switch (c) {
	case '\n': inspect_text(out, "\\n"); break;
	case '\r': inspect_text(out, "\\r"); break;
	case '\t': inspect_text(out, "\\t"); break;
	case ' ':  inspect_text(out, "\\s"); break;
	case '\\': inspect_text(out, "\\\\"); break;
	case '"':  inspect_text(out, "\\\""); break;
	default:
		if (c < 33 || c > 126) {
			snprintf(escaped, sizeof(escaped), "\\x%02x", c);
			inspect_text(out, escaped);
		} else {
			escaped[0] = (char)c;
			escaped[1] = 0;
			inspect_text(out, escaped);
		}
	}
}

static void inspect_value (struct inspection *out, skO *obj, unsigned depth)
{
	skO *node;
	char number[32];
	const unsigned char *name;
	unsigned characters;

	if (out->used + 1 >= out->size) {
		out->shortened = 1;
		return;
	}
	switch (obj->tag) {
	case SKO_NUMBER:
		if (!isfinite(obj->data.number)) {
			inspect_text(out, isnan(obj->data.number) ? "Number(NaN)" :
				(obj->data.number < 0 ? "Number(-Infinity)" : "Number(Infinity)"));
			break;
		}
		snprintf(number, sizeof(number), "%.17g", obj->data.number);
		inspect_text(out, number);
		break;
	case SKO_BOOLEAN:
		inspect_text(out, obj->data.boolean ? "TRUE" : "FALSE");
		break;
	case SKO_CHARACTER:
		inspect_text(out, "'");
		inspect_byte(out, (unsigned char)obj->data.character);
		break;
	case SKO_QSYMBOL:
	case SKO_SYMBOL:
		if (obj->tag == SKO_QSYMBOL)
			inspect_text(out, ":");
		else
			inspect_text(out, "Symbol(");
		name = (const unsigned char *)obj->data.sym->name;
		while (*name && !out->shortened)
			inspect_byte(out, *name++);
		if (obj->tag == SKO_SYMBOL)
			inspect_text(out, ")");
		break;
	case SKO_LIST:
		/* Recognize only short complete strings; never scan an unbounded list. */
		node = obj->data.list;
		characters = 0;
		while (node && node->tag == SKO_CHARACTER && characters < 64) {
			characters++;
			node = node->next;
		}
		if (!node && characters) {
			inspect_text(out, "\"");
			for (node = obj->data.list; node && !out->shortened; node = node->next)
				inspect_byte(out, (unsigned char)node->data.character);
			inspect_text(out, "\"");
			break;
		}
		inspect_text(out, "[");
		node = obj->data.list;
		if (node && !depth)
			inspect_text(out, "...");
		else while (node && !out->shortened) {
			inspect_value(out, node, depth - 1);
			node = node->next;
			if (node)
				inspect_text(out, " ");
		}
		inspect_text(out, "]");
		break;
	}
}

void skO_inspect (skO *obj, char *buffer, size_t size, unsigned depth)
{
	struct inspection out;

	if (!size)
		return;
	out.buffer = buffer;
	out.size = size;
	out.used = 0;
	out.shortened = 0;
	inspect_value(&out, obj, depth);
	if (out.shortened && size >= 4) {
		out.used = size - 1;
		memcpy(buffer + out.used - 3, "...", 3);
	}
	buffer[out.used] = 0;
}
