#ifndef RIPPLE_H
#define RIPPLE_H

#include "../../shirka.h"

#define RIPPLE_INPUT_LIMIT (1024 * 1024)

struct ripple {
	skE *session;
	context *scope;
	char *source;
	skO *pending;
	int running;
	int interactive;
	int failed;
};

extern struct ripple ripple;
extern volatile sig_atomic_t ripple_interrupt;
extern volatile sig_atomic_t ripple_stop;

const char *ripple_command_name (char *source);

void ripple_terminal_init (void);
void ripple_terminal_free (void);
int ripple_read (void);
void ripple_prompt (char *buffer, size_t size);
void ripple_stack (void);
void ripple_evaluate (int file);

#endif
