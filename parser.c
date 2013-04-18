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
#include <setjmp.h>
#include "shirka.h"

#ifdef SK_PARSER_DEBUG
#include <stdio.h>
#endif

/*//////////////////////////////////////////////////////////////////////////*/

/*
The following macros are used by token extractors as character categories.
*/

#define IDENTIFIER_START(c) (isalpha(c)                                      \
	|| c == '*' || c == '+' || c == '-' || c == '/' || c == '<'          \
	|| c == '=' || c == '>' || c == '^' || c == '!' || c == '&'          \
	|| c == '.' || c == '?' || c == '|' || c == '~' || c == '$'          \
	|| c == '%')

#define IDENTIFIER_CONT(c) (IDENTIFIER_START(c) || isdigit(c) || c == '\'')

/*//////////////////////////////////////////////////////////////////////////*/

/*
The only function of the parser available to the outside world.
*/
skO *skO_parse (char **next, jmp_buf jmp, char *delim);

/*
Token extractors
*/
skO *parse_number            (char **next);
skO *parse_character         (char **next, jmp_buf jmp);
skO *parse_character_literal (char **next, jmp_buf jmp);
skO *parse_qidentifier       (char **next);
skO *parse_identifier        (char **next);
skO *parse_op_def            (char **next);
skO *parse_obj_reserve       (char **next);
skO *parse_obj_restore       (char **next);
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
		if (isspace(*c)) {
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

skO *parse_number (char **next)
{
	char *c = *next;
	char buffer[SYMBOL_MAX_LENGTH];
	short i = 0;

	if (*c == '-' && c[1] >= '0' && c[1] <= '9') {
		buffer[i] = *c;
		i++;
		c++;
	}

	if (*c == '.')
		return NULL;

	while (1) {
		if (!isdigit(*c))
			break;
		buffer[i] = *c;
		i++;
		c++;
	}

	if (*c == '.') {
		buffer[i] = *c;
		i++;
		c++;

		while (1) {
			if (!isdigit(*c))
				break;
			buffer[i] = *c;
			i++;
			c++;
		}
	}

	buffer[i] = 0;

	if (!i)
		return NULL;
	char **dummy = NULL;
	*next = c;
	#ifdef SK_PARSER_DEBUG
	printf("Parsed NUMBER:      %s\n", buffer);
	#endif
	return skO_number_new(strtod(buffer, dummy));
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

	if (*c == '\\') {
		c++;
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
			FATAL("PANIC! Unknown escape sequence \\%c.\n", *c);
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

skO *parse_qidentifier (char **next)
{
	char *c = *next;
	char buffer[SYMBOL_MAX_LENGTH];
	short i = 0;

	if (*c != ':')
		goto failure;

	c++;

	if (!IDENTIFIER_START(*c))
		goto failure;

	buffer[i] = *c;
	i++;
	c++;

	while (1) {
		if (c[0] == '-' && c[1] == '-')
			break;

		if (IDENTIFIER_CONT(*c)) {
			buffer[i] = *c;
			i++;
			c++;
		} else {
			buffer[i] = 0;
			break;
		}
	}

	*next = c;
	#ifdef SK_PARSER_DEBUG
	printf("Parsed QIDENTIFIER: %s\n", buffer);
	#endif
	return skO_quoted_symbol_new(buffer);

failure:
	return NULL;
}

skO *parse_identifier (char **next)
{
	char *c = *next;
	char buffer[SYMBOL_MAX_LENGTH];
	short i = 0;

	if (!IDENTIFIER_START(*c))
		goto failure;

	buffer[i] = *c;
	i++;
	c++;

	while (1) {
		if (c[0] == '-' && c[1] == '-')
			break;

		if (IDENTIFIER_CONT(*c)) {
			buffer[i] = *c;
			i++;
			c++;
		} else {
			buffer[i] = 0;
			break;
		}
	}

	*next = c;
	#ifdef SK_PARSER_DEBUG
	printf("Parsed IDENTIFIER:  %s\n", buffer);
	#endif
	return skO_symbol_new(buffer);

failure:
	return NULL;
}

skO *parse_op_def (char **next)
{
	char *src = *next;
	skO *sym;

	if (src[0] == '=' && src[1] == '>') {
		src++;
		src++;
		consume_leading(&src);
		sym = parse_identifier(&src);
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

skO *parse_obj_reserve (char **next)
{
	char *src = *next;
	skO *sym;

	if (src[0] == '-' && src[1] == '>') {
		src++;
		src++;
		consume_leading(&src);
		sym = parse_identifier(&src);
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

skO *parse_obj_restore (char **next)
{
	char *src = *next;
	skO *sym;

	if (src[0] == '<' && src[1] == '-') {
		src++;
		src++;
		consume_leading(&src);
		sym = parse_identifier(&src);
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

	if (**next != '"')
		return NULL;

	++*next;
	list = skO_list_new();

	while (**next != '"')
		sk_list_append(list, parse_character(next, jmp));
	++*next;

	#ifdef SK_PARSER_DEBUG
	printf("Parsed STRING\n");
	#endif
	return list;
}

skO *skO_parse (char **next, jmp_buf jmp, char *delim)
{
	skO     *obj;                       /* parsed token (or NULL)       */
	skO     *list     = skO_list_new(); /* used to store parsed objects */
	skO     *prefixed = NULL;           /* store "prefix sugar" tokens  */
	char    *src      = *next;
	jmp_buf pe;
	int     ended     = 0;

	consume_leading(&src);

	if (setjmp(pe)) {
		if (prefixed)
			free(prefixed);
		skO_free(list);
		printf("parse jmp\n");
	}

	if (delim) {
		if (*src != delim[0])
			return NULL;
		src++;
		consume_leading(&src);
		#ifdef SK_PARSER_DEBUG
		printf("Parsed %c\n", delim[0]);
		#endif
	} else if (*src == 0) {
		ended = 1;
		exit(EXIT_FAILURE);
	}

	while (!ended) {
		consume_leading(&src);

		if (*src == 0) {
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
		if (prefixed)
			consume_leading(&src);

		/* Handle "reserving operations" syntactic sugar. */

		obj = parse_op_def(&src);
		if (obj) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$=>");
			goto matched;
		}

		obj = parse_obj_reserve(&src);
		if (obj) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$->");
			goto matched;
		}

		obj = parse_obj_restore(&src);
		if (obj) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$<-");
			goto matched;
		}

		/* Handle regular tokens. */

		if ((obj = parse_string_literal(&src, pe))
			|| (obj = parse_number(&src))
			|| (obj = parse_qidentifier(&src))
			|| (obj = parse_identifier(&src))
			|| (obj = parse_character_literal(&src, pe))
			|| (obj = skO_parse(&src, pe, "[]")))
			goto matched;

		/* If everything failed... */

		fprintf(stderr, "PANIC! Parsing error: %s\n", src);
		longjmp(jmp, 1);

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
