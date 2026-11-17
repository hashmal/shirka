#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ripple.h"

static char *history_path;
static int initialized;
static struct termios original_terminal;
static int saved_terminal;

static void signal_handler (int signal_number)
{
	ripple_interrupt = 1;
	if (signal_number != SIGINT)
		ripple_stop = 1;
}

static int editing_interrupt (void)
{
	if (ripple_interrupt) {
		rl_replace_line("", 0);
		rl_done = 1;
	}
	return 0;
}

static void history_init (void)
{
	const char *base = getenv("XDG_STATE_HOME");
	const char *home = getenv("HOME");
	char *cursor;
	struct stat info;
	size_t size;
	int fd;

	if (base && *base != '/')
		base = NULL;
	if (!base && (!home || *home != '/'))
		return;
	size = strlen(base ? base : home) + 40;
	history_path = malloc(size);
	if (!history_path)
		return;
	snprintf(history_path, size, "%s%s/ripple/history", base ? base : home,
		base ? "" : "/.local/state");
	for (cursor = history_path + 1; *cursor; cursor++) {
		if (*cursor == '/') {
			*cursor = 0;
			mkdir(history_path, 0700);
			*cursor = '/';
		}
	}
	/* Do not follow a preexisting history-file symlink. */
	if (lstat(history_path, &info) == 0 &&
		(!S_ISREG(info.st_mode) || info.st_uid != geteuid())) {
		free(history_path);
		history_path = NULL;
		return;
	}
	fd = open(history_path, O_WRONLY | O_CREAT, 0600);
	if (fd < 0) {
		free(history_path);
		history_path = NULL;
		return;
	}
	fchmod(fd, 0600);
	close(fd);
	history_comment_char = '#';
	history_write_timestamps = 1;
	history_multiline_entries = 1;
	stifle_history(500);
	if (stat(history_path, &info) == 0 && info.st_size <= 4 * 1024 * 1024)
		read_history(history_path);
}

void ripple_terminal_init (void)
{
	struct sigaction action;

	ripple.interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) &&
		getenv("TERM") && strcmp(getenv("TERM"), "dumb");
	memset(&action, 0, sizeof(action));
	action.sa_handler = signal_handler;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGHUP, &action, NULL);
	if (ripple.interactive) {
		saved_terminal = tcgetattr(STDIN_FILENO, &original_terminal) == 0;
		rl_readline_name = "ripple";
		rl_catch_signals = 0;
		rl_event_hook = editing_interrupt;
		rl_signal_event_hook = editing_interrupt;
		rl_initialize();
		rl_variable_bind("enable-bracketed-paste", "on");
		history_init();
	}
	initialized = 1;
}

void ripple_terminal_free (void)
{
	if (initialized && ripple.interactive) {
		if (history_path)
			write_history(history_path);
		clear_history();
		rl_free_line_state();
		rl_cleanup_after_signal();
		if (saved_terminal)
			tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);
	}
	free(history_path);
	history_path = NULL;
}

void ripple_prompt (char *buffer, size_t size)
{
	skO *values[3];
	skO *node = ripple.session->stack;
	char value[128];
	size_t used = 0;
	int count = 0;
	int written;

	if (!size)
		return;
	buffer[0] = 0;
	while (node && count < 3) {
		values[count++] = node;
		node = node->next;
	}
	while (count) {
		skO_inspect(values[--count], value, sizeof(value), 3);
		written = snprintf(buffer + used, size - used, "%s ", value);
		if (written < 0 || (size_t)written >= size - used)
			return;
		used += (size_t)written;
	}
	snprintf(buffer + used, size - used, "> ");
}

void ripple_history (const char *text)
{
	char stamp[32];

	if (ripple.interactive && *text && strlen(text) <= 8192) {
		add_history(text);
		snprintf(stamp, sizeof(stamp), "#%ld", (long)time(NULL));
		add_history_time(stamp);
	}
}

/* Consume an overlong physical line, but never silently truncate it. */
static char *stream_line (enum ripple_input *status)
{
	char *line = malloc(RIPPLE_INPUT_LIMIT + 1);
	size_t length = 0;
	int c;
	int overflow = 0;

	if (!line) {
		fprintf(stderr, "ripple: could not allocate input.\n");
		*status = RIPPLE_ERROR;
		return NULL;
	}
	while ((c = fgetc(stdin)) != EOF && c != '\n') {
		if (length < RIPPLE_INPUT_LIMIT)
			line[length++] = c ? (char)c : 127;
		else
			overflow = 1;
	}
	if (overflow || (c == EOF && ferror(stdin) && !ripple_interrupt)) {
		fprintf(stderr, "ripple: input read failed or exceeds 1 MiB.\n");
		*status = overflow ? RIPPLE_INVALID : RIPPLE_ERROR;
		free(line);
		return NULL;
	}
	if (c == EOF && !length) {
		*status = RIPPLE_EOF;
		free(line);
		return NULL;
	}
	line[length] = 0;
	return line;
}

/* One editor interaction only; the Shirka controller owns pending input. */
char *ripple_read_line (int continuation, enum ripple_input *status)
{
	char prompt[512];
	char *line;
	char *cursor;

	*status = RIPPLE_EOF;
	if (ripple_stop)
		return NULL;
	ripple_interrupt = 0;
	if (continuation)
		strcpy(prompt, "... ");
	else
		ripple_prompt(prompt, sizeof(prompt));
	*status = RIPPLE_LINE;
	line = ripple.interactive ? readline(prompt) : stream_line(status);
	if (ripple_interrupt) {
		free(line);
		clearerr(stdin);
		if (ripple.interactive)
			putchar('\n');
		*status = ripple_stop ? RIPPLE_EOF : RIPPLE_CANCEL;
		ripple_interrupt = 0;
		return NULL;
	}
	if (!line) {
		if (*status == RIPPLE_LINE)
			*status = RIPPLE_EOF;
		return NULL;
	}
	for (cursor = line; *cursor; cursor++) {
		if (((unsigned char)*cursor < 32 && *cursor != '\n' && *cursor != '\t') ||
			(unsigned char)*cursor == 127)
			break;
	}
	if (*cursor || strlen(line) > RIPPLE_INPUT_LIMIT) {
		fprintf(stderr, "ripple: input contains control bytes or exceeds 1 MiB.\n");
		free(line);
		*status = RIPPLE_INVALID;
		return NULL;
	}
	return line;
}
