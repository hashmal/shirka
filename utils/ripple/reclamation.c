/* Exercise the actual host and controller with deterministic terminal input.
 * Only the interpreter allocator is counted; interned symbols are retained. */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ripple.h"
#include "../../test/reclamation.h"

int ripple_main (int argc, char **argv);

static size_t allocations;
static size_t baseline;
static int submission;
static int cycle;
static char nul_command[256];

void *sk_test_alloc (size_t size)
{
	void *pointer = malloc(size);

	assert(pointer);
	allocations++;
	return pointer;
}

void sk_test_free (void *pointer)
{
	if (pointer) {
		assert(allocations);
		allocations--;
	}
	free(pointer);
}

void ripple_terminal_init (void)
{
	ripple.interactive = 0;
	/* Exercise styled prompt construction without depending on a real TTY. */
	ripple.color = 1;
}

void ripple_terminal_free (void) {}
void ripple_history (const char *text) { (void)text; }

char *ripple_read_line (const char *prompt, enum ripple_input *status)
{
	const char *inputs[] = {
		"1 increment", "/clear", "1 TRUE +", "/clear",
		"[1 -> temporary missing 2] !", "[ : ]",
		"/load test/fixtures/absent.shk", "/load test/fixtures/malformed.shk",
		"[", "2", "]", cycle % 20 ? "/stack" : "/fields", "/clear",
		cycle % 20 ? "/clear" : "/bindings", nul_command, "/clear"
	};
	const char *text;

	*status = RIPPLE_LINE;
	assert(ripple.session->scope == ripple.scope && !ripple.session->execution);
	if (submission == -1) {
		submission = 0;
		return strdup("42 -> persistent [1 +] => increment 1 -> removed <- removed <<");
	}
	assert((!strcmp(prompt, "... ")) == (submission == 9 || submission == 10));
	if (!submission) {
		assert(!ripple.session->stack);
		/* Compare the same input pattern after inspection and stack display
		 * have both warmed up; their controller temporaries differ in size. */
		if (cycle == 20)
			baseline = allocations;
		if (cycle > 20 && cycle % 20 == 0)
			assert(allocations == baseline);
		if (cycle == 100) {
			*status = RIPPLE_EOF;
			return NULL;
		}
	}
	text = inputs[submission++];
	if ((size_t)submission == sizeof(inputs) / sizeof(inputs[0])) {
		submission = 0;
		cycle++;
	}
	return strdup(text);
}

int main (void)
{
	size_t retained;
	char path[] = "/tmp/ripple-nul-XXXXXX";
	const char source[] = "999 puts\0abort";
	int fd = mkstemp(path);
	FILE *file;
	int i;

	assert(fd >= 0);
	file = fdopen(fd, "wb"); assert(file);
	assert(fwrite(source, 1, sizeof(source) - 1, file) == sizeof(source) - 1);
	assert(fclose(file) == 0);
	snprintf(nul_command, sizeof(nul_command), "/load %s", path);
	retained = 0;
	for (i = 0; i < 2; i++) {
		submission = -1;
		cycle = 0;
		ripple_interrupt = ripple_stop = 0;
		assert(ripple_main(1, NULL) == EXIT_FAILURE);
		if (i)
			assert(allocations == retained);
		retained = allocations;
	}
	unlink(path);
	puts("ripple controller and session reclamation passed; interned symbols retained.");
	return 0;
}
