#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <termios.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include "ripple.h"

static char output[65536];
static size_t used;
static int terminal;
static pid_t child;
static int tty_session;
static int colors;
static char state_path[] = "/tmp/ripple-test-XXXXXX";

static void expect (const char *text)
{
	fd_set readers;
	struct timeval timeout;
	ssize_t count;

	while (!strstr(output, text)) {
		FD_ZERO(&readers);
		FD_SET(terminal, &readers);
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		if (select(terminal + 1, &readers, NULL, NULL, &timeout) != 1 ||
			used + 1 >= sizeof(output)) {
			fprintf(stderr, "Expected %s; transcript:\n%s\n", text, output);
			kill(child, SIGKILL);
			abort();
		}
		count = read(terminal, output + used, sizeof(output) - used - 1);
		assert(count > 0);
		used += (size_t)count;
		output[used] = 0;
	}
}

static void expect_prompt (const char *preview)
{
	char text[2048];

	snprintf(text, sizeof(text), "%s%s", preview,
		colors ? "\033[90m..\033[39m " : ".. ");
	expect(text);
}

static void send_text (const char *text)
{
	used = 0;
	output[0] = 0;
	assert(write(terminal, text, strlen(text)) == (ssize_t)strlen(text));
}

static void start (int interactive, const char *input)
{
	struct winsize window = {24, 80, 0, 0};
	struct termios cooked;
	int pipe_in[2];
	int pipe_out[2];

	used = 0;
	output[0] = 0;
	tty_session = interactive;
	memset(&cooked, 0, sizeof(cooked));
	cooked.c_iflag = ICRNL | IXON;
	cooked.c_oflag = OPOST | ONLCR;
	cooked.c_cflag = CS8 | CREAD;
	cooked.c_lflag = ISIG | ICANON | ECHO | IEXTEN;
	cooked.c_cc[VINTR] = 3;
	cooked.c_cc[VEOF] = 4;
	cooked.c_cc[VERASE] = 127;
	cooked.c_cc[VMIN] = 1;
	cfsetispeed(&cooked, B38400);
	cfsetospeed(&cooked, B38400);
	if (interactive)
		child = forkpty(&terminal, NULL, &cooked, &window);
	else {
		assert(pipe(pipe_in) == 0 && pipe(pipe_out) == 0);
		child = fork();
	}
	assert(child >= 0);
	if (!child) {
		setenv("XDG_STATE_HOME", state_path, 1);
		setenv("TERM", "xterm", 1);
		if (colors)
			unsetenv("NO_COLOR");
		else
			setenv("NO_COLOR", "1", 1);
		if (!interactive) {
			dup2(pipe_in[0], STDIN_FILENO);
			dup2(pipe_out[1], STDOUT_FILENO);
			dup2(pipe_out[1], STDERR_FILENO);
			close(pipe_in[0]); close(pipe_in[1]);
			close(pipe_out[0]); close(pipe_out[1]);
		}
		alarm(60);
		execl("utils/ripple/ripple", "ripple", (char *)NULL);
		_exit(127);
	}
	if (interactive)
		expect_prompt("");
	else {
		close(pipe_in[0]); close(pipe_out[1]);
		terminal = pipe_out[0];
		assert(write(pipe_in[1], input, strlen(input)) == (ssize_t)strlen(input));
		close(pipe_in[1]);
	}
}

static void finish (int status)
{
	int result;
	char discard[4096];
	fd_set readers;
	struct timeval timeout;
	struct termios settings;

	/* Drain redisplay output while the child restores its terminal. */
	for (;;) {
		FD_ZERO(&readers);
		FD_SET(terminal, &readers);
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		assert(select(terminal + 1, &readers, NULL, NULL, &timeout) == 1);
		if (read(terminal, discard, sizeof(discard)) <= 0)
			break;
	}
	assert(waitpid(child, &result, 0) == child);
	assert(WIFEXITED(result) && WEXITSTATUS(result) == status);
	if (tty_session) {
		assert(tcgetattr(terminal, &settings) == 0);
		assert((settings.c_lflag & (ICANON | ECHO)) == (ICANON | ECHO));
	}
	close(terminal);
}

static void unit_tests (void)
{
	char text[32];
	skO *list;
	skO *value;

	assert(skO_read("[ : ]", &list) == SK_PARSE_ERROR);
	assert(skO_read("[", &list) == SK_PARSE_MORE);
	assert(skO_read("(puts)", &list) == SK_PARSE_MORE);
	assert(skO_read("[ '\\n -- ]\n", &list) == SK_PARSE_MORE);
	assert(skO_read("[ '[ '] ]", &list) == SK_PARSE_OK);
	skO_free(list);
	assert(skO_read("\"a\\\"[\"", &list) == SK_PARSE_OK);
	skO_free(list);
	assert(skO_read("\"a\\\"[", &list) == SK_PARSE_MORE);
	assert(skO_read("\"\\q", &list) == SK_PARSE_ERROR);
	assert(skO_read("'", &list) == SK_PARSE_MORE);
	assert(skO_read("'\\", &list) == SK_PARSE_MORE);
	assert(skO_read("\"\\03", &list) == SK_PARSE_MORE);
	assert(skO_read("'\\400", &list) == SK_PARSE_ERROR);
	assert(skO_read("\"\\777\"", &list) == SK_PARSE_ERROR);
	assert(skO_read("'\\0", &list) == SK_PARSE_OK);
	assert(list->data.list->data.character == 0);
	skO_free(list);
	assert(skO_read("'\\0334", &list) == SK_PARSE_OK);
	assert(list->data.list->data.character == 27);
	assert(list->data.list->next->data.number == 4);
	skO_free(list);
value = skO_symbol_new("TRUE");
	skO_inspect(value, text, sizeof(text), 3);
	assert(!strcmp(text, "Symbol(TRUE)"));
	assert(value->tag == SKO_SYMBOL);
	skO_free(value);
	value = skO_quoted_symbol_new("TRUE");
	skO_inspect(value, text, sizeof(text), 3);
	assert(!strcmp(text, ":TRUE"));
	skO_free(value);
	value = skO_character_new(27);
	skO_inspect(value, text, sizeof(text), 3);
	assert(!strcmp(text, "'\\x1b"));
	skO_free(value);
	assert(skO_read("[1 [2 [3]]]", &list) == SK_PARSE_OK);
	skO_inspect(list->data.list, text, sizeof(text), 1);
	assert(!strcmp(text, "[1 [...]]"));
	skO_inspect(list, text, 8, 3);
	assert(strstr(text, "..."));
	skO_free(list);
}

int main (void)
{
	struct timeval pause = {0, 200000};
	char history[256];
	char load_path[256];
	char command[300];
	FILE *file;

	unit_tests();
	assert(mkdtemp(state_path));
	start(0, "2\n3\n+\n42 -> x\nx puts\n[1 +] => inc\ninc\n/stack\n");
	expect("0: 6");
	assert(strstr(output, "42") && !strchr(output, 27));
	finish(0);
	snprintf(history, sizeof(history), "%s/ripple/history", state_path);
	assert(access(history, F_OK) != 0); /* Pipes never write history. */
	start(0, "+\nmissing\n1 TRUE +\nabort\n/clear\n[\n1 '[ -- ]\n]\n/stack\n");
	expect("0: [1 '[]");
	assert(!strchr(output, 27));
	finish(1);
	start(0, "[\n");
	expect("incomplete input at EOF");
	finish(1);
	/* Commands are whole primary inputs, not tokens inside submitted code. */
	start(0, "\"/exit\" puts\n-- /exit\n[\n/exit\n]\n/stack\n/clear\n"
		"[8] => /help\n/help puts\n(puts)\n9\n/load\t\n"
		"/load test/fixtures/absent.shk\n/load test/fixtures/malformed.shk\n"
		"77 -> survived abort\nsurvived puts\n/exit\n999 puts\n");
	expect("77");
	assert(strstr(output, "[/exit]") && strstr(output, "8\n9\n"));
	assert(strstr(output, "Usage: /load PATH") && !strstr(output, "999"));
	finish(1);
	start(0, "[1] 300 times\n/stack\n/clear\n/stack\n");
	expect("(empty stack)");
	assert(strstr(output, "255: 1") && !strstr(output, "256: 1"));
	assert(strstr(output, "additional stack values omitted"));
	finish(0);

snprintf(load_path, sizeof(load_path), "%s/load data.shk", state_path);
	file = fopen(load_path, "w"); assert(file);
	fputs("77 -> loaded [2 *] => twice", file); fclose(file);
	start(1, NULL);
	snprintf(command, sizeof(command), "/load %s\n", load_path);
	send_text(command); expect_prompt("");
	send_text("loaded twice\n"); expect_prompt("154 ");
	send_text("/clear\n"); expect_prompt("");
	send_text("(!)[1 -> x []]\n"); expect_prompt("[] ");
	send_text(".x\n"); expect_prompt("[] 1 ");
	send_text("puts\n"); expect_prompt("[] ");
	send_text("42 -> .x\n"); expect_prompt("[] ");
	send_text("[.x 2 *] => .double\n"); expect_prompt("[] ");
	send_text(".double\n"); expect_prompt("[] 84 ");
	send_text("/clear\n"); expect_prompt("");
	send_text("[ : ]\n"); expect("Parsing error"); expect_prompt("");
	send_text("2\n"); expect_prompt("2 ");
	send_text("3\n"); expect_prompt("2 3 ");
	send_text("[]\n"); expect_prompt("2 3 [] ");
	send_text("/clear\n"); expect_prompt("");
	send_text(":puts :puts unquote\n"); expect_prompt(":puts puts ");
	send_text("/clear\n"); expect_prompt("");
	send_text("42 -> x\n"); expect_prompt("");
	send_text("x\n"); expect_prompt("42 ");
	send_text("[1 +] => inc\n"); expect_prompt("42 ");
	send_text("inc\n"); expect_prompt("43 ");
	send_text("missing\n"); expect_prompt("43 ");
	send_text("/clear\n"); expect_prompt("");
	send_text("12\033[D3\033[F\n"); expect_prompt("132 ");
	send_text("9\033[H1\033[F2\n"); expect_prompt("132 192 ");
	send_text("123\033[H\033[C\033[3~\n"); expect_prompt("132 192 13 ");
	send_text("12\1773\n"); expect_prompt("192 13 13 ");
	send_text("/clear\n"); expect_prompt("");
	send_text("12\033[H\004\n"); expect_prompt("2 ");
	send_text("7\n"); expect_prompt("2 7 ");
	send_text("\033[A\n"); expect_prompt("2 7 7 ");
	send_text("/clear\n"); expect_prompt("");
	send_text("\033[200~1\n2 +\033[201~\n"); expect_prompt("3 ");
	send_text("[\n"); expect("... ");
	send_text("\003"); expect_prompt("3 ");
	send_text("discard this");
	select(0, NULL, NULL, NULL, &pause);
	send_text("\003"); expect_prompt("3 ");
	send_text("[loop] => loop loop\n");
	pause.tv_sec = 0; pause.tv_usec = 200000;
	select(0, NULL, NULL, NULL, &pause);
	send_text("\003"); expect_prompt("3 ");
send_text("12 34\033b5\n"); expect_prompt("3 12 534 ");
	send_text("/clear\n"); expect_prompt("");
	send_text("\033[200~\"\033[31m\"\033[201~\n");
	expect("control bytes"); expect_prompt("");
	assert(!strstr(output, "\033[31m"));
	send_text("9\n"); expect_prompt("9 ");
	send_text("/exit\n");
	finish(0);
	file = fopen(history, "r"); assert(file); fclose(file);
	start(1, NULL);
	send_text("\033[A\033[A\n"); expect_prompt("9 ");
	send_text("\004"); finish(0);
	start(1, NULL);
	assert(kill(child, SIGTERM) == 0);
	finish(0);
	colors = 1;
	start(0, "\"hello\" :foo 12\n/stack\n");
	expect("2: \"hello\"");
	assert(!strchr(output, 27)); /* Streams stay plain even without NO_COLOR. */
	finish(0);
	start(1, NULL);
	send_text(":foo \"hello\" 12\n");
	expect_prompt("\033[31m:foo\033[39m \033[32m\"hello\"\033[39m \033[36m12\033[39m ");
	send_text("/clear\n"); expect_prompt("");
	send_text("[x :y \"z\" TRUE 3 '[ ]\n");
	expect("[x \033[31m:y\033[39m \033[32m\"z\"\033[39m");
	expect_prompt("TRUE \033[36m3\033[39m \033[32m'[\033[39m] ");
	send_text("/stack\n"); expect("0: [x"); expect_prompt("");
	/* Editing with a colored, long prompt must still use visible columns. */
	send_text("12\033[H3\033[F\n"); expect_prompt("\033[36m312\033[39m ");
	send_text("missing\n"); expect_prompt("\033[36m312\033[39m ");
	send_text("1                                                                                \033[H2\033[F\n");
	expect_prompt("\033[36m21\033[39m ");
	send_text("\003"); expect_prompt("\033[36m21\033[39m ");
	send_text("/clear\n"); expect_prompt("");
	send_text("\033[A\n"); expect_prompt("");
	send_text("TRUE\n"); expect_prompt("\033[35mTRUE\033[39m ");
	send_text("/exit\n"); finish(0);
	file = fopen(history, "r"); assert(file);
	while (!feof(file))
		assert(fgetc(file) != 27); /* History stores source, never color bytes. */
	fclose(file);
	unlink(history);
	unlink(load_path);
	snprintf(history, sizeof(history), "%s/ripple", state_path);
	rmdir(history); rmdir(state_path);
	puts("ripple stream, inspection, and terminal regressions passed.");
	return 0;
}
