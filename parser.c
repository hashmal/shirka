/* Copyright (c) 2013, Jeremy Pinat. */

/*
Parser
======

This parser is used to build object lists from strings. Shirka's grammar is
very simple, and so is its parser (it doesn't use any external parser
generator tool).

The process is as follow:

1. Create an empty list
2. Try *token extractors* on the start of the input string
3. If an object is returned, append it to the list
4. Repeat the process from point **2** until the string ends or there is a
   syntax error

*Token extractors* generate objects from the beginning of a string. If a token
extractor cannot generate an object, it returns `NULL`. If it can, it returns
that object and **advances the string pointer to the end of the matched
sub-string**.

Syntactic sugar is handled in this parser. Comments are ignored.

------------------------------------------------------------------------------

To enable the printing of debug information, define the constant
`SK_PARSER_DEBUG` when compiling the interpreter.
*/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "shirka.h"

#ifdef SK_PARSER_DEBUG
#include <stdio.h>
#endif

/*//////////////////////////////////////////////////////////////////////////*/

/*
The following macros are used by token extractors as character categories.
*/

#define IDENTIFIER_START(c) (isalpha((unsigned char)(c)) || c == '_'                          \
	|| c == '!' || c == '#' || c == '$'  || c == '%' || c == '&'         \
	|| c == '*' || c == '+' || c == ','  || c == '-'         \
	|| c == '/' || c == ';' || c == '<'  || c == '=' || c == '>'         \
	|| c == '?' || c == '@' || c == '\\' || c == '^' || c == '|'         \
	|| c == '~')

#define IDENTIFIER_CONT(c) (IDENTIFIER_START(c) || isdigit((unsigned char)(c)) || c == '\'')

#define SEPARATOR(c) (isspace((unsigned char)(c))                                             \
	|| c == '[' || c == ']' || c == '(' || c == ')' || c == 0)

/*//////////////////////////////////////////////////////////////////////////*/

/*
The only function of the parser available to the outside world.
*/
skO *skO_parse (char **next, jmp_buf jmp, char *delim);

/*
Token extractors
*/
skO *parse_number            (char **next, jmp_buf jmp);
skO *parse_character         (char **next, jmp_buf jmp);
skO *parse_character_literal (char **next, jmp_buf jmp);
skO *parse_qidentifier       (char **next, jmp_buf jmp);
skO *parse_identifier        (char **next, jmp_buf jmp);
skO *parse_path              (char **next, jmp_buf jmp);
skO *parse_op_def            (char **next, jmp_buf jmp);
skO *parse_obj_reserve       (char **next, jmp_buf jmp);
skO *parse_obj_restore       (char **next, jmp_buf jmp);
skO *parse_string_literal    (char **next, jmp_buf jmp);

/*
Consume leading whitespace and comments. `next' is set to point to the next
non-whitespace, non-comment character in the source string.
*/
void consume_leading (char **next);

/*//////////////////////////////////////////////////////////////////////////*/

void consume_leading (char **next)
{
	char *c = *next;

	while (*c != 0) {
		if (isspace((unsigned char)*c)) {
			c++;
		} else if (c[0] == '-' && c[1] == '-') {
			while (*c != '\n' && *c != 0)
				c++;
		} else {
			break;
		}
	}
	*next = c;
}

skO *parse_number (char **next, jmp_buf jmp)
{
	char *c = *next;
	char buffer[SYMBOL_MAX_LENGTH];
	size_t i = 0;

	if (*c == '-' && c[1] >= '0' && c[1] <= '9') {
		if (i >= sizeof(buffer) - 1) {
			fprintf(stderr, "PANIC! Token too long.\n");
			longjmp(jmp, 1);
		}
		buffer[i] = *c;
		i++;
		c++;
	}

	if (*c == '.')
		return NULL;

	while (1) {
		if (!isdigit((unsigned char)*c))
			break;
		if (i >= sizeof(buffer) - 1) {
			fprintf(stderr, "PANIC! Token too long.\n");
			longjmp(jmp, 1);
		}
		buffer[i] = *c;
		i++;
		c++;
	}

	if (*c == '.') {
		if (i >= sizeof(buffer) - 1) {
			fprintf(stderr, "PANIC! Token too long.\n");
			longjmp(jmp, 1);
		}
		buffer[i] = *c;
		i++;
		c++;

		while (1) {
			if (!isdigit((unsigned char)*c))
				break;
			if (i >= sizeof(buffer) - 1) {
				fprintf(stderr, "PANIC! Token too long.\n");
				longjmp(jmp, 1);
			}
			buffer[i] = *c;
			i++;
			c++;
		}
	}

	buffer[i] = 0;

	if (SEPARATOR(*c) && i) {
		char **dummy = NULL;
		*next = c;
		#ifdef SK_PARSER_DEBUG
		printf("Parsed NUMBER:      %s\n", buffer);
		#endif
		return skO_number_new(strtod(buffer, dummy));
	} else {
		return NULL;
	}
}

skO *parse_character_literal (char **next, jmp_buf jmp)
{
	if (**next != '\'')
		return NULL;

	++*next;
	return parse_character(next, jmp);

}

skO *parse_character (char **next, jmp_buf jmp)
{
	char *c = *next;
	char result = 0;

	if (*c == 0)
		longjmp(jmp, SK_PARSE_MORE);

	if (*c == '\\') {
		c++;
		if (*c == 0)
			longjmp(jmp, SK_PARSE_MORE);
		if (*c >= '0' && *c <= '7') {
			unsigned int value = 0;
			int digits = 0;

			while (digits < 3 && *c >= '0' && *c <= '7') {
				value = value * 8 + (unsigned int)(*c++ - '0');
				digits++;
			}
			if (value > 255) {
				fprintf(stderr, "PANIC! Octal escape exceeds one byte.\n");
				longjmp(jmp, SK_PARSE_ERROR);
			}
			*next = c;
			return skO_character_new((char)value);
		}
		switch (*c) {
		case '\\':
			result = '\\';
			break;
		case '"':
			result = '"';
			break;
		case 'a':
			result = '\a';
			break;
		case 'b':
			result = '\b';
			break;
		case 'f':
			result = '\f';
			break;
		case 'n':
			result = '\n';
			break;
		case 'r':
			result = '\r';
			break;
		case 's':
			result = ' ';
			break;
		case 't':
			result = '\t';
			break;
		case 'v':
			result = '\v';
			break;
		default:
			fprintf(stderr, "PANIC! Unknown escape sequence \\%c.\n", *c);
			longjmp(jmp, 1);
		}
		#ifdef SK_PARSER_DEBUG
		printf("Parsed CHARACTER:   \\%c\n", *c);
		#endif
	} else {
		result = *c;
		#ifdef SK_PARSER_DEBUG
		printf("Parsed CHARACTER:   %c\n", *c);
		#endif
	}

	c++;
	*next = c;
	return skO_character_new(result);
}

skO *parse_qidentifier (char **next, jmp_buf jmp)
{
	char *cursor = *next;
	skO *symbol;

	if (*cursor != ':')
		return NULL;
	cursor++;
	symbol = parse_path(&cursor, jmp);
	if (!symbol)
		return NULL;
	symbol->tag = SKO_QSYMBOL;
	*next = cursor;
	return symbol;
}

skO *parse_identifier (char **next, jmp_buf jmp)
{
	char *c = *next;
	char buffer[SYMBOL_MAX_LENGTH];
	size_t i = 0;

	if (!IDENTIFIER_START(*c))
		goto failure;

	buffer[i] = *c;
	i++;
	c++;

	while (1) {
		if (c[0] == '-' && c[1] == '-')
			break;

		if (IDENTIFIER_CONT(*c)) {
			if (i >= sizeof(buffer) - 1) {
				fprintf(stderr, "PANIC! Token too long.\n");
				longjmp(jmp, 1);
			}
			buffer[i] = *c;
			i++;
			c++;
		} else {
			break;
		}
	}

	buffer[i] = 0;
	*next = c;
	#ifdef SK_PARSER_DEBUG
	printf("Parsed IDENTIFIER:  %s\n", buffer);
	#endif
	return skO_symbol_new(buffer);

failure:
	return NULL;
}

/* Dot separates identifiers; an initial dot selects the stack record. Keep paths
 * serialized in symbols so quotation, cons and reserve operations stay uniform. */
skO *parse_path (char **next, jmp_buf jmp)
{
	char *cursor = *next;
	char *start;
	char *end;
	char buffer[SYMBOL_MAX_LENGTH];
	size_t length;
	skO *component;

	if (*cursor != '.') {
		component = parse_identifier(&cursor, jmp);
		if (!component)
			return NULL;
		if (*cursor != '.') {
			*next = cursor;
			return component;
		}
		skO_free(component);
	}
	while (*cursor == '.') {
		start = ++cursor;
		if (!IDENTIFIER_START(*cursor) || (cursor[0] == '-' && cursor[1] == '-')) {
			fprintf(stderr, "PANIC! Dot must be followed by an identifier.\n");
			longjmp(jmp, SK_PARSE_ERROR);
		}
		/* Check the complete path's bound before allocating another component. */
		end = start + 1;
		while (IDENTIFIER_CONT(*end) && !(end[0] == '-' && end[1] == '-'))
			end++;
		if ((size_t)(end - *next) >= sizeof(buffer)) {
			fprintf(stderr, "PANIC! Path too long.\n");
			longjmp(jmp, SK_PARSE_ERROR);
		}
		component = parse_identifier(&cursor, jmp);
		skO_free(component);
	}
	length = (size_t)(cursor - *next);
	memcpy(buffer, *next, length);
	buffer[length] = 0;
	*next = cursor;
	return skO_symbol_new(buffer);
}

skO *parse_op_def (char **next, jmp_buf jmp)
{
	char *src = *next;
	skO *sym;

	if (src[0] == '=' && src[1] == '>') {
		src++;
		src++;
		consume_leading(&src);
		sym = parse_path(&src, jmp);
		if (sym) {
			*next = src;
			sym->tag = SKO_QSYMBOL;
			#ifdef SK_PARSER_DEBUG
			printf("Parsed $=>\n");
			#endif
			return(sym);
		}
	}

	return NULL;
}

skO *parse_obj_reserve (char **next, jmp_buf jmp)
{
	char *src = *next;
	skO *sym;

	if (src[0] == '-' && src[1] == '>') {
		src++;
		src++;
		consume_leading(&src);
		sym = parse_path(&src, jmp);
		if (sym) {
			*next = src;
			sym->tag = SKO_QSYMBOL;
			#ifdef SK_PARSER_DEBUG
			printf("Parsed $->\n");
			#endif
			return(sym);
		}
	}

	return NULL;
}

skO *parse_obj_restore (char **next, jmp_buf jmp)
{
	char *src = *next;
	skO *sym;

	if (src[0] == '<' && src[1] == '-') {
		src++;
		src++;
		consume_leading(&src);
		sym = parse_path(&src, jmp);
		if (sym) {
			*next = src;
			sym->tag = SKO_QSYMBOL;
			#ifdef SK_PARSER_DEBUG
			printf("Parsed $<-\n");
			#endif
			return(sym);
		}
	}

	return NULL;
}

skO *parse_string_literal (char **next, jmp_buf jmp)
{
	skO *list;
	jmp_buf string_error;
	int status;

	if (**next != '"')
		return NULL;

	++*next;
	list = skO_list_new();
	list->literal = 1;

	if ((status = setjmp(string_error))) {
		skO_free(list);
		longjmp(jmp, status);
	}
	while (**next != '"') {
		if (**next == 0) {
			longjmp(string_error, SK_PARSE_MORE);
		}
		sk_list_append(list, parse_character(next, string_error));
	}
	++*next;

	#ifdef SK_PARSER_DEBUG
	printf("Parsed STRING\n");
	#endif
	return list;
}

skO *skO_parse (char **next, jmp_buf jmp, char *delim)
{
	skO     *obj;                       /* parsed token (or NULL)       */
	skO     *list;                     /* used to store parsed objects */
	/* Preserve ownership of pending prefix tokens across longjmp. */
	skO     *volatile prefixed = NULL;
	char    *src      = *next;
	jmp_buf pe;
	int     status;
	int     ended     = 0;

	consume_leading(&src);
	if (delim && *src != delim[0])
		return NULL;
	list = skO_list_new();
	list->literal = 1;

	if ((status = setjmp(pe))) {
		if (prefixed)
			skO_free(prefixed);
		skO_free(list);
		longjmp(jmp, status);
	}

	if (delim) {
		src++;
		consume_leading(&src);
		#ifdef SK_PARSER_DEBUG
		printf("Parsed %c\n", delim[0]);
		#endif
	} else if (*src == 0) {
		ended = 1;
	}

	while (!ended) {
		consume_leading(&src);

		if (*src == 0) {
			if (delim) {
				longjmp(pe, SK_PARSE_MORE);
			}
			ended = 1;
			*next = src;
			break;
		}

		/* Handle end of lists and prefixed syntax. */

		if (delim) {
			if (*src == delim[1]) {
				src++;
				*next = src;
				#ifdef SK_PARSER_DEBUG
				printf("Parsed %c\n", delim[1]);
				#endif
				return list;
			}
		}

		/* Handle "prefix style" syntactic sugar. */

		prefixed = skO_parse(&src, pe, "()");
		if (prefixed) {
			consume_leading(&src);
			if (*src == 0)
				longjmp(pe, SK_PARSE_MORE);
		}

		/* Handle "reserving operations" syntactic sugar. */

		obj = parse_op_def(&src, pe);
		if (obj) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$=>");
			goto matched;
		}

		obj = parse_obj_reserve(&src, pe);
		if (obj) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$->");
			goto matched;
		}

		obj = parse_obj_restore(&src, pe);
		if (obj) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$<-");
			goto matched;
		}

		/* Handle regular tokens. */

		if ((obj = parse_string_literal(&src, pe))
			|| (obj = parse_number(&src, pe))
			|| (obj = parse_qidentifier(&src, pe))
			|| (obj = parse_path(&src, pe))
			|| (obj = parse_character_literal(&src, pe))
			|| (obj = skO_parse(&src, pe, "[]")))
			goto matched;

		/* If everything failed... */

		fprintf(stderr, "PANIC! Parsing error: %s\n", src);
		longjmp(pe, 1);

	matched:
		sk_list_append(list, obj);
		
		if (prefixed) {
			sk_list_append(list, prefixed->data.list);
			free(prefixed);
			prefixed = NULL;
		}
	}

	*next = src;
	return list;
}

/* Read once using the real parser; partial trees are reclaimed by its handlers. */
int skO_read (char *source, skO **result)
{
	char *cursor = source;
	jmp_buf error;
	int status;

	*result = NULL;
	if ((status = setjmp(error)))
		return status;
	*result = skO_parse(&cursor, error, NULL);
	return SK_PARSE_OK;
}
