/* Copyright (C) 2013, Jeremy Pinat. */

#include <stdlib.h>
#include <stdio.h>
#include "shirka.h"

/* #define SK_PARSER_DEBUG */

/*////////////////////////////////////////////////////////////////////////////
//                       MACROS FOR CHARACTER CLASSES                       //
////////////////////////////////////////////////////////////////////////////*/

#define LETTER_UP(c) (c >= 'A' && c <= 'Z')

#define LETTER_DOWN(c) (c >= 'a' && c <= 'z')

#define LETTER(c) (LETTER_UP(c) || LETTER_DOWN(c) || c == '_')

#define DIGIT(c) (c >= '0' && c <= '9')

#define WHITESPACE(c) (c == ' ' || c == '\t' || c == '\n')

#define IDENTIFIER_START(c) (LETTER(c)                                       \
	|| c == '*' || c == '+' || c == '-' || c == '/' || c == '<'          \
	|| c == '=' || c == '>' || c == '^' || c == '!' || c == '&'          \
	|| c == '.' || c == '?' || c == '|' || c == '~' || c == '$')

#define IDENTIFIER_CONT(c) (IDENTIFIER_START(c) || DIGIT(c) || c == '\'')

/*////////////////////////////////////////////////////////////////////////////
//                             TOKEN EXTRACTION                             //
////////////////////////////////////////////////////////////////////////////*/

/*
 * Consume leading whitespace and comments. `next' is set to point to the next
 * non-whitespace, non-comment character in the source string.
 */
void consume_leading (char **next)
{
	char *c = *next;

	while (*c != 0) {
		if (WHITESPACE(*c)) {
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
		if (DIGIT(*c)) {
			buffer[i] = *c;
			i++;
			c++;
		} else {
			break;
		}
	}

	if (*c == '.') {
		buffer[i] = *c;
		i++;
		c++;

		while (1) {
			if (DIGIT(*c)) {
				buffer[i] = *c;
				i++;
				c++;
			} else {
				break;
			}
		}
	}

	buffer[i] = 0;

	if (i) {
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

skO *parse_character (char **next)
{
	char *c = *next;
	char char_literal;

	if (*c != '\'')
		return NULL;

	c++;
	if (*c == '\\') {
		c++;
		if (*c == 'n') {
			#ifdef SK_PARSER_DEBUG
			printf("Parsed CHARACTER:   \\%c\n", *c);
			#endif
			c++;
			*next = c;
			return skO_character_new('\n');
		} else {
			printf("PANIC! Unknown escape sequence.\n");
			exit(EXIT_FAILURE);
		}
	} else {
		char_literal = *c;
		c++;
		*next = c;
		#ifdef SK_PARSER_DEBUG
		printf("Parsed CHARACTER:   %c\n", *c);
		#endif
		return skO_character_new(char_literal);
	}
}

skO *parse_qidentifier (char **next)
{
	char *c = *next;
	char buffer[SYMBOL_MAX_LENGTH];
	short i = 0;

	if (*c != ':')
		goto failure;

	c++;

	if (IDENTIFIER_START(*c)) {
		buffer[i] = *c;
		i++;
		c++;
	} else {
		goto failure;
	}

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

	if (IDENTIFIER_START(*c)) {
		buffer[i] = *c;
		i++;
		c++;
	} else {
		goto failure;
	}

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
		if (sym != NULL) {
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
		if (sym != NULL) {
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
		if (sym != NULL) {
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

skO *parse_string (char **next)
{
	skO *list;
	char *c = *next;

	if (*c != '"')
		return NULL;

	c++;
	list = skO_list_new();

	while (*c != '"') {
		if (*c == '\\') {
			c++;
			switch (*c) {
			case 'n':
				sk_list_append(list, skO_character_new('\n'));
				break;
			default:
				printf("PANIC! Unknown escape sequence.\n");
				exit(EXIT_FAILURE);
			}
		} else {
			sk_list_append(list, skO_character_new(*c));
		}
		c++;
	}
	c++;

	*next = c;
	#ifdef SK_PARSER_DEBUG
	printf("Parsed STRING\n");
	#endif
	return list;
}

/*////////////////////////////////////////////////////////////////////////////
//                                 PARSING                                  //
////////////////////////////////////////////////////////////////////////////*/

/*
 * Parse a list in a string.
 */
skO *parse_list (char **next)
{
	char *src = *next;
	skO *obj;

	if (*src == '[') {
		#ifdef SK_PARSER_DEBUG
		printf("Parsed [\n");
		#endif
		src++;
		obj = skO_parse(&src);

		if (*src == ']') {
			src++;
			*next = src;
			#ifdef SK_PARSER_DEBUG
			printf("Parsed ]\n");
			#endif
		}

		return obj;
	} else {
		return NULL;
	}
}

skO *skO_parse (char **next)
{
	skO *obj;                       /* parsed token (or NULL)       */
	skO *list     = skO_list_new(); /* used to store tokens         */
	skO *prefixed = NULL;           /* store "prefix" syntax tokens */
	char   *src      = *next;

	while (*src != 0) {
		consume_leading(&src);
		if (*src == 0)
			break;

		/* Handle "prefix style" syntactic sugar. */

		if(*src == '(') {
			src++;
			prefixed = skO_parse(&src);
			if (*src == ')') {
				src++;
				consume_leading(&src);
			}
		}

		/* Handle "reserving operations" syntactic sugar. */

		obj = parse_op_def(&src);
		if (obj != NULL) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$=>");
			goto matched;
		}

		obj = parse_obj_reserve(&src);
		if (obj != NULL) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$->");
			goto matched;
		}

		obj = parse_obj_restore(&src);
		if (obj != NULL) {
			sk_list_append(list, obj);
			obj = skO_symbol_new("$<-");
			goto matched;
		}

		/* Handle regular tokens. */

		obj = parse_string(&src);
		if (obj != NULL) goto matched;

		obj = parse_number(&src);
		if (obj != NULL) goto matched;

		obj = parse_qidentifier(&src);
		if (obj != NULL) goto matched;

		obj = parse_identifier(&src);
		if (obj != NULL) goto matched;

		obj = parse_character(&src);
		if (obj != NULL) goto matched;

		obj = parse_list(&src);
		if (obj != NULL) goto matched;

		/* Handle end of lists and prefixed syntax. */

		if (*src == ']' || *src == ')')
			break;

		/* If everything failed... */

		printf("PANIC! Parsing error: %s\n", src);
		exit(EXIT_FAILURE);

	matched:
		sk_list_append(list, obj);
		if (prefixed != NULL) {
			sk_list_append(list, prefixed->data.list);
			free(prefixed);
			prefixed = NULL;
		}
	}

	*next = src;
	return list;
}
