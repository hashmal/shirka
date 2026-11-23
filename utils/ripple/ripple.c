#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ripple.h"
#include "../../hashmap.h"

#ifndef RIPPLE_HOME
#define RIPPLE_HOME "."
#endif

struct ripple ripple;
volatile sig_atomic_t ripple_interrupt;
volatile sig_atomic_t ripple_stop;

/* Validate and copy text without consuming the controller's character list. */
static char *text_copy (skE *env, skO *text)
{
	skO *node;
	char *buffer;
	size_t length = 0;

	skE_checkType(env, text, SKO_LIST);
	for (node = text->data.list; node; node = node->next) {
		skE_checkType(env, node, SKO_CHARACTER);
		if (!node->data.character || length == RIPPLE_INPUT_LIMIT)
			skE_error(env, "ripple: text contains NUL or exceeds 1 MiB");
		length++;
	}
	buffer = malloc(length + 1);
	if (!buffer)
		skE_error(env, "ripple: could not allocate text");
	length = 0;
	for (node = text->data.list; node; node = node->next)
		buffer[length++] = node->data.character;
	buffer[length] = 0;
	return buffer;
}

static char *text_pop (skE *env)
{
	char *text = text_copy(env, skE_stackPeek(env, 0));

	skO_free(skE_stackPop(env));
	return text;
}

static skO *text_new (const char *text)
{
	skO *list = skO_list_new();
	skO **tail = &list->data.list;

	/* External text is data, not a closure over the controller's current step. */
	list->scope = skL_new(NULL);
	while (text && *text) {
		*tail = skO_character_new(*text++);
		tail = &(*tail)->next;
	}
	return list;
}

/* Own instructions; recover on the actual session stack, never try's. */
static int evaluate (skO *list)
{
	skE *env = ripple.session;

	if (ripple_stop || ripple_interrupt) {
		if (list)
			skO_free(list);
		ripple_interrupt = 0;
		return 0;
	}
	if (setjmp(env->jmp)) {
		skE_unwind(env, ripple.scope);
		ripple_interrupt = 0;
		return 0;
	}
	skE_execList(env, list, 0);
	ripple_interrupt = 0;
	return 1;
}

/* prompt -- text :line|:cancel|:eof|:invalid|:error */
static skO *read_line (skE *env)
{
	char *prompt = text_pop(env);
	enum ripple_input status;
	char *text;
	const char *names[] = {"line", "cancel", "eof", "invalid", "error"};

	text = ripple_read_line(prompt, &status);
	free(prompt);
	skE_stackPush(env, text_new(text));
	free(text);
	skE_stackPush(env, skO_quoted_symbol_new((char *)names[status]));
	return NULL;
}

/* text -- instructions :ready|:more|:invalid */
static skO *parse (skE *env)
{
	char *text = text_pop(env);
	skO *list;
	int status = skO_read(text, &list);

	free(text);
	skE_stackPush(env, list ? list : skO_list_new());
	skE_stackPush(env, skO_quoted_symbol_new(status == SK_PARSE_OK ? "ready" :
		status == SK_PARSE_MORE ? "more" : "invalid"));
	return NULL;
}

/* instructions -- success? */
static skO *execute (skE *env)
{
	skE_checkType(env, skE_stackPeek(env, 0), SKO_LIST);
	skE_stackPush(env, skO_boolean_new(evaluate(skE_stackPop(env))));
	return NULL;
}

/* path -- instructions success?; parsing never executes or changes the session. */
static skO *parse_file (skE *env)
{
	char *path = text_pop(env);
	skO *list;

	if (setjmp(ripple.session->jmp))
		list = NULL;
	else
		list = skO_loadParse(ripple.session, path);

	free(path);
	skE_stackPush(env, list ? list : skO_list_new());
	skE_stackPush(env, skO_boolean_new(list != NULL));
	return NULL;
}

static int view_limit (skE *env, size_t depth, int maximum)
{
	skO *value = skE_stackPeek(env, depth);

	skE_checkType(env, value, SKO_NUMBER);
	if (!(value->data.number >= 1 && value->data.number <= maximum))
		skE_error(env, "ripple: invalid stack inspection limit");
	return (int)value->data.number;
}

/* count width depth -- descriptions omitted?; never copies values. */
static skO *stack_view (skE *env)
{
	skO *node = ripple.session->stack;
	skO *list;
	skO **tail;
	char value[512];
	size_t count = 0;
	int depth = view_limit(env, 0, 6);
	int width = view_limit(env, 1, 511);
	int limit = view_limit(env, 2, 256);

	list = skO_list_new();
	list->scope = skL_new(NULL);
	tail = &list->data.list;
	skO_free(skE_stackPop(env));
	skO_free(skE_stackPop(env));
	skO_free(skE_stackPop(env));
	while (node && count++ < (size_t)limit) {
		skO_inspect(node, value, (size_t)width + 1, (unsigned)depth);
		*tail = text_new(value);
		tail = &(*tail)->next;
		node = node->next;
	}
	skE_stackPush(env, list);
	skE_stackPush(env, skO_boolean_new(node != NULL));
	return NULL;
}

/* Inspection records own only bounded text and metadata, not session values. */
static void field (skO *record, char *name, skO *value)
{
	skO *key = skO_quoted_symbol_new(name);

	if (!skH_set(record->scope->map, key->data.sym, value, SKH_VALUE))
		FATAL("ripple: could not allocate inspection field.\n");
	skO_free(key);
}

struct binding_view {
	skL *scope;
	skL *current;
	skO **tail;
	int count;
	int width;
	int depth;
	int omitted;
};

static int describe_binding (const skH_binding *binding, void *data)
{
	struct binding_view *view = data;
	skO *record;
	skO key = {0};
	char text[512];
	char *kind;

	if (skH_get(view->scope->map, binding->key) != binding)
		return 1;
	if (!view->count--) {
		view->omitted = 1;
		return 0;
	}
	record = skO_list_new();
	record->scope = skL_new(NULL);
	key.tag = SKO_QSYMBOL;
	key.data.sym = binding->key;
	skO_inspect(&key, text, (size_t)view->width + 1, 1);
	field(record, "name", text_new(text));
	kind = binding->kind == SKH_OPERATION ? "operation" :
		binding->kind == SKH_NATIVE ? "native" :
		binding->kind == SKH_HIDDEN ? "hidden" : "value";
	field(record, "kind", skO_quoted_symbol_new(kind));
	field(record, "origin", skO_quoted_symbol_new(view->current == view->scope ? "local" : "captured"));
	if (binding->value)
		skO_inspect(binding->value, text, (size_t)view->width + 1, (unsigned)view->depth);
	else
		snprintf(text, sizeof(text), "%s", binding->kind == SKH_HIDDEN ? "<hidden>" : "<native>");
	field(record, "value", text_new(text));
	*view->tail = record;
	view->tail = &record->next;
	return 1;
}

/* path count width depth -- entry-records omitted? found?
 * Empty path selects session bindings; '.' selects the stack's top record. */
static skO *bindings (skE *env)
{
	struct binding_view view;
	char *path;
	skO *result;
	skO *object = NULL;
	skO *key;
	const skH_binding *binding;
	int found = 1;
	int i;

	view.depth = view_limit(env, 0, 6);
	view.width = view_limit(env, 1, 511);
	view.count = view_limit(env, 2, 256);
	path = text_copy(env, skE_stackPeek(env, 3));
	for (i = 0; i < 4; i++)
		skO_free(skE_stackPop(env));
	view.scope = ripple.session->scope->bindings;
	if (*path) {
		if (path[0] == '.' && (!ripple.session->stack || ripple.session->stack->tag != SKO_LIST))
			found = 0;
		else if (!strcmp(path, "."))
			object = ripple.session->stack;
		else if (strlen(path) >= SYMBOL_MAX_LENGTH)
			found = 0;
		else {
			key = skO_symbol_new(path);
			binding = skE_lookup(ripple.session, key->data.sym);
			object = binding ? binding->value : NULL;
			skO_free(key);
		}
		if (!object || object->tag != SKO_LIST)
			found = 0;
		view.scope = found ? object->scope : NULL;
	}
	free(path);
	if (!found)
		fprintf(stderr, "ripple: field inspection requires an existing list or record.\n");
	result = skO_list_new();
	result->scope = skL_new(NULL);
	view.tail = &result->data.list;
	view.omitted = 0;
	for (view.current = view.scope; view.current; view.current = view.current->ancestor)
		if (!skH_each(view.current->map, describe_binding, &view))
			break;
	skE_stackPush(env, result);
	skE_stackPush(env, skO_boolean_new(view.omitted));
	skE_stackPush(env, skO_boolean_new(found));
	return NULL;
}

static skO *clear_stack (skE *env)
{
	(void)env;
	while (ripple.session->stack)
		skO_free(skE_stackPop(ripple.session));
	return NULL;
}

/* text -- ; storage is native, deciding what to remember is self-hosted. */
static skO *remember (skE *env)
{
	char *text = text_pop(env);

	ripple_history(text);
	free(text);
	return NULL;
}

int main (int argc, char **argv)
{
	skE *control;
	volatile int status = EXIT_SUCCESS;

	(void)argv;
	if (argc != 1) {
		fprintf(stderr, "Usage: ripple (or pipe Shirka input into ripple)\n");
		return EXIT_FAILURE;
	}
	ripple.session = skE_new();
	skE_init(ripple.session);
	control = skE_new();
	skE_init(control);
	if (setjmp(control->jmp)) {
		status = EXIT_FAILURE;
	} else {
		skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/lib/prelude.shk"), 0);
		skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/lib/ansi.shk"), 0);
		if (setjmp(ripple.session->jmp)) {
			status = EXIT_FAILURE;
		} else {
			skE_execList(ripple.session,
				skO_loadParse(ripple.session, RIPPLE_HOME "/lib/prelude.shk"), 0);
			skE_scopePush(ripple.session);
			ripple.scope = ripple.session->scope;
			ripple.session->interrupt = &ripple_interrupt;
			ripple_terminal_init();
			skE_defNative(control, "ripple/read-line", read_line);
			skE_defNative(control, "ripple/parse", parse);
			skE_defNative(control, "ripple/execute", execute);
			skE_defNative(control, "ripple/parse-file", parse_file);
			skE_defNative(control, "ripple/stack-view", stack_view);
			skE_defNative(control, "ripple/clear-stack", clear_stack);
			skE_defNative(control, "ripple/history", remember);
			skE_defNative(control, "ripple/bindings", bindings);
			skE_defObject(control, skO_quoted_symbol_new("ripple/color?"),
				skO_boolean_new(ripple.color));
			skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/utils/ripple/highlight.shk"), 0);
			skE_execList(control, skO_loadParse(control, RIPPLE_HOME "/utils/ripple/ripple.shk"), 0);
			/* The controller leaves its accumulated success flag on top. */
			skE_checkType(control, skE_stackPeek(control, 0), SKO_BOOLEAN);
			if (!ripple.interactive && !control->stack->data.boolean)
				status = EXIT_FAILURE;
		}
	}
	ripple_terminal_free();
	skE_free(control);
	skE_free(ripple.session);
	return status;
}
