/* Check the self-hosted harness from outside Shirka: make test-harness.
 * Each child has a five-second timeout. Output is displayed only on failure.
 */
#define _POSIX_C_SOURCE 200809L
/* This driver's checks must remain enabled even in a release build. */
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

struct test_case {
	char *source;
	int status;
	char *message;
};

static void check (struct test_case *test)
{
	char path[] = "/tmp/shirka-harness-XXXXXX";
	char output[8192];
	int fd = mkstemp(path);
	FILE *source;
	FILE *log = tmpfile();
	pid_t child;
	int status;
	size_t length;

	assert(fd >= 0 && log);
	source = fdopen(fd, "w");
	assert(source);
	assert(fputs("\"lib/test.shk\" with\n", source) >= 0);
	assert(fputs(test->source, source) >= 0);
	assert(fclose(source) == 0);
	fflush(NULL);
	child = fork();
	assert(child >= 0);
	if (child == 0) {
		if (dup2(fileno(log), STDOUT_FILENO) < 0 ||
			dup2(fileno(log), STDERR_FILENO) < 0)
			_exit(126);
		fclose(log);
		alarm(5);
		execl("./shirka", "shirka", path, (char *)NULL);
		_exit(127);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(unlink(path) == 0);
	rewind(log);
	length = fread(output, 1, sizeof(output) - 1, log);
	output[length] = 0;
	fclose(log);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != test->status ||
		!strstr(output, test->message) ||
		(test->status && (strstr(output, "-- Test suite passed.") ||
		strstr(output, "AFTER-FAILURE")))) {
		fprintf(stderr, "Harness regression failed:\n%s\n%s\n",
			test->source, output);
		exit(EXIT_FAILURE);
	}
}

int main (void)
{
	struct test_case tests[] = {
		{ "[[1 1] assert_equal] test/run", 0, "Test suite passed." },
		{ "[[1 2] assert_different] test/run", 0, "Test suite passed." },
		{ "[[TRUE FALSE] assert_different] test/run", 0, "Test suite passed." },
		{ "[['a 'b] assert_different] test/run", 0, "Test suite passed." },
		{ "[[[1 [2]] [1 [2]]] assert_equal] test/run", 0, "Test suite passed." },
		{ "[[1 TRUE +] assert_error] test/run", 0, "Test suite passed." },
		{ "[[abort] assert_error] test/run", 0, "Test suite passed." },
		{ "[[\"[ : ]\" $parse] assert_error] test/run", 0, "Test suite passed." },
		{ "[[1 2] assert_equal] test/run", 1,
			"Expected: 2\n    Result: 1" },
		{ "[[1 2] assert_equal \"AFTER-FAILURE\" puts] test/run",
			1, "Equality failed:" },
		{ "[[1 1] assert_different] test/run", 1, "Difference failed:" },
		{ "[[1] assert_error] test/run", 1, "Error was expected" },
		{ "[[] assert_error] test/run", 1, "Error was expected" },
		{ "[[missing] assert_equal] test/run", 1, "Unexpected error" },
		{ "[[missing] assert_different] test/run", 1, "Unexpected error" },
		{ "[[] assert_equal] test/run", 1, "exactly two results" },
		{ "[[1] assert_equal] test/run", 1, "exactly two results" },
		{ "[[1 2 3] assert_equal] test/run", 1, "exactly two results" },
		{ "[[] assert_different] test/run", 1, "exactly two results" },
		{ "[[1] assert_different] test/run", 1, "exactly two results" },
		{ "[[1 2 3] assert_different] test/run", 1, "exactly two results" },
		{ "[1 assert_different] test/run", 1, "requires a quotation" },
		{ "[1 assert_equal] test/run", 1, "requires a quotation" },
		{ "[1 assert_error] test/run", 1, "requires a quotation" },
		{ "[assert_equal] test/run", 1, "Test suite failed." },
		{ "[missing] test/run", 1, "Test suite failed." },
		{ "[1] test/run", 1, "unexpected values" },
		{ "1 test/run", 1, "requires a quotation" },
		{ "[] test/run [] test/run", 0, "Test suite passed." },
		{ "99 [[1 1] assert_equal] test/run 99 = not [abort] !?",
			0, "Test suite passed." },
		{ "[[1 1] assert_equal [[2 2] assert_equal] test/run] test/run",
			0, "Test suite passed." }
	};
	size_t i;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
		check(&tests[i]);
	puts("Test harness regressions passed.");
	return 0;
}
