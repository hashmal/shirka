#ifndef RIPPLE_H
#define RIPPLE_H

#include "../../shirka.h"

#define RIPPLE_INPUT_LIMIT (1024 * 1024)

enum ripple_input {
	RIPPLE_LINE,
	RIPPLE_CANCEL,
	RIPPLE_EOF,
	RIPPLE_INVALID,
	RIPPLE_ERROR
};

struct ripple {
	skE *session;
	context *scope;
	int interactive;
	int color;
};

extern struct ripple ripple;
extern volatile sig_atomic_t ripple_interrupt;
extern volatile sig_atomic_t ripple_stop;

void ripple_terminal_init (void);
void ripple_terminal_free (void);
char *ripple_read_line (const char *prompt, enum ripple_input *status);
void ripple_history (const char *text);

#endif
