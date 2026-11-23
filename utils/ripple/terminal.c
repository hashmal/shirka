#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ripple.h"

/* Raw record separators cannot occur in accepted editor input. */
#define HISTORY_MARKER '\036'

static int history_directory = -1;
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
	char *path;
	char *cursor;
	char descriptor[64];
	char first = 0;
	struct stat info;
	size_t size;
	int fd;
	int status;

	history_comment_char = HISTORY_MARKER;
	history_write_timestamps = 1;
	history_multiline_entries = 1;
	stifle_history(500);
	if (base && *base != '/')
		base = NULL;
	if (!base && (!home || *home != '/'))
		return;
	size = strlen(base ? base : home) + 40;
	path = malloc(size);
	if (!path)
		return;
	snprintf(path, size, "%s%s/ripple/history", base ? base : home,
		base ? "" : "/.local/state");
	for (cursor = path + 1; *cursor; cursor++) {
		if (*cursor == '/') {
			*cursor = 0;
			mkdir(path, 0700);
			*cursor = '/';
		}
	}
	*strrchr(path, '/') = 0;
	history_directory = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	free(path);
	if (history_directory < 0)
		return;
	if (fstat(history_directory, &info) < 0 || info.st_uid != geteuid() ||
		(info.st_mode & 077))
		goto unavailable;
	/* Pin the checked file before Readline opens it through /dev/fd. */
	fd = openat(history_directory, "history", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		if (errno == ENOENT)
			return;
		goto unavailable;
	}
	if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid()) {
		close(fd);
		goto unavailable;
	}
	snprintf(descriptor, sizeof(descriptor), "/dev/fd/%d", fd);
	if (info.st_size > 4 * 1024 * 1024 || pread(fd, &first, 1, 0) < 0) {
		close(fd);
		goto unavailable;
	}
	/* Read existing '#' timestamp files; all future saves use our marker. */
	history_comment_char = first == HISTORY_MARKER ? HISTORY_MARKER : '#';
	status = read_history(descriptor);
	history_comment_char = HISTORY_MARKER;
	close(fd);
	if (status != 0)
		goto unavailable;
	return;

unavailable:
	close(history_directory);
	history_directory = -1;
}

static void history_save (void)
{
	char temporary[64];
	HIST_ENTRY **entries = history_list();
	FILE *file;
	int fd = -1;
	int success;
	int i;

	if (history_directory < 0)
		return;
	for (i = 0; i < 16; i++) {
		snprintf(temporary, sizeof(temporary), ".history.%ld.%d", (long)getpid(), i);
		fd = openat(history_directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd >= 0 || errno != EEXIST)
			break;
	}
	if (fd < 0)
		return;
	file = fdopen(fd, "w");
	if (!file) {
		close(fd);
		unlinkat(history_directory, temporary, 0);
		return;
	}
	/* Readline's path-based writer cannot use a pinned directory on macOS.
	 * Its timestamp-delimited format also preserves multiline submissions. */
	success = fchmod(fd, 0600) == 0;
	for (i = 0; success && entries && entries[i]; i++) {
		const char *stamp = entries[i]->timestamp;

		success = fprintf(file, "%c%s\n%s\n", HISTORY_MARKER,
			stamp && *stamp ? stamp + 1 : "0", entries[i]->line) >= 0;
	}
	if (fclose(file) != 0)
		success = 0;
	/* Publish a complete file; failure leaves the previous history intact. */
	if (success)
		renameat(history_directory, temporary, history_directory, "history");
	unlinkat(history_directory, temporary, 0);
}

void ripple_terminal_init (void)
{
	struct sigaction action;

	ripple.interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) &&
		getenv("TERM") && strcmp(getenv("TERM"), "dumb");
	ripple.color = ripple.interactive && !getenv("NO_COLOR");
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
		history_save();
		clear_history();
		rl_free_line_state();
		rl_cleanup_after_signal();
		if (saved_terminal)
			tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);
	}
	if (history_directory >= 0)
		close(history_directory);
	history_directory = -1;
	initialized = saved_terminal = 0;
}

void ripple_history (const char *text)
{
	char stamp[32];

	if (ripple.interactive && *text && strlen(text) <= 8192) {
		add_history(text);
		snprintf(stamp, sizeof(stamp), "%c%ld", HISTORY_MARKER, (long)time(NULL));
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
char *ripple_read_line (const char *prompt, enum ripple_input *status)
{
	char *marked;
	char *destination;
	const char *source;
	const char *end;
	char *line;
	char *cursor;

	*status = RIPPLE_EOF;
	if (ripple_stop)
		return NULL;
	ripple_interrupt = 0;
	/* Readline must exclude SGR bytes when measuring prompt columns. */
	marked = malloc(strlen(prompt) * 3 + 1);
	if (!marked) {
		fprintf(stderr, "ripple: could not allocate prompt.\n");
		*status = RIPPLE_ERROR;
		return NULL;
	}
	destination = marked;
	for (source = prompt; *source;) {
		end = source;
		if (source[0] == 27 && source[1] == '[') {
			end = source + 2;
			while ((*end >= '0' && *end <= '9') || *end == ';')
				end++;
			if (*end == 'm') {
				*destination++ = RL_PROMPT_START_IGNORE;
				do { *destination++ = *source++; } while (source <= end);
				*destination++ = RL_PROMPT_END_IGNORE;
				continue;
			}
		}
		*destination++ = *source++;
	}
	*destination = 0;
	*status = RIPPLE_LINE;
	line = ripple.interactive ? readline(marked) : stream_line(status);
	free(marked);
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
