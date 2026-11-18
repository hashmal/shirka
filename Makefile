CFLAGS+=-std=c99 -pedantic -Wall -Wextra -Wdeclaration-after-statement

shirka: Makefile
env.o: env.c intrinsics.c shirka.h
objects.o: objects.c shirka.h
parser.o: parser.c shirka.h

shirka: shirka.c shirka.h env.o objects.o parser.o intrinsics.c
	$(CC) $(CFLAGS) -o shirka shirka.c env.o objects.o parser.o

.PHONY: clean test test-reclamation test-harness

clean:
	rm -f *.o
	rm -f shirka utils/ripple/ripple

test: shirka test-harness test-hashmap
	./shirka test/parser.shk
	./shirka test/operations.shk
	./shirka test/ansi.shk
	./shirka test/ripple-highlight.shk

.PHONY: test-hashmap
test-hashmap: env.o objects.o parser.o
	@set -e; \
	build_dir=$$(mktemp -d); \
	trap 'rm -rf "$$build_dir"' 0; \
	$(CC) $(CFLAGS) -I. -include test/hashmap-alloc.h \
		-Dmalloc=skH_test_alloc -Dfree=skH_test_free \
		-c hashmap.c -o "$$build_dir/hashmap.o"; \
	$(CC) $(CFLAGS) -I. test/hashmap.c "$$build_dir/hashmap.o" \
		env.o objects.o parser.o -lm -o "$$build_dir/test"; \
	"$$build_dir/test"

# Count allocations independently of the Shirka test framework.
test-reclamation:
	@set -e; \
	build_dir=$$(mktemp -d); \
	trap 'rm -rf "$$build_dir"' 0; \
	for source in env objects parser; do \
		$(CC) $(CFLAGS) -I. -include test/reclamation.h \
			-Dmalloc=sk_test_alloc -Dfree=sk_test_free \
			-c $$source.c -o "$$build_dir/$$source.o"; \
	done; \
	$(CC) $(CFLAGS) -I. test/reclamation.c "$$build_dir/"*.o \
		-lm -o "$$build_dir/test"; \
	"$$build_dir/test"

# Verify exit status and diagnostics independently of Shirka assertions.
test-harness: shirka
	@set -e; \
	build_dir=$$(mktemp -d); \
	trap 'rm -rf "$$build_dir"' 0; \
	$(CC) $(CFLAGS) test/harness.c -o "$$build_dir/test"; \
	"$$build_dir/test"

# GNU Readline is used only by ripple. Override these on other installations.
READLINE_PREFIX ?= $(firstword $(wildcard /opt/homebrew/opt/readline /usr/local/opt/readline))
RIPPLE_CPPFLAGS ?= $(if $(READLINE_PREFIX),-I$(READLINE_PREFIX)/include)
RIPPLE_LIBS ?= $(if $(READLINE_PREFIX),-L$(READLINE_PREFIX)/lib) -lreadline -lm

.PHONY: ripple
ripple: utils/ripple/ripple

utils/ripple/ripple: utils/ripple/ripple.c utils/ripple/terminal.c utils/ripple/ripple.h utils/ripple/ripple.shk utils/ripple/highlight.shk lib/ansi.shk env.o objects.o parser.o
	$(CC) $(CFLAGS) $(RIPPLE_CPPFLAGS) -DRIPPLE_HOME='"$(CURDIR)"' \
		-o $@ utils/ripple/ripple.c utils/ripple/terminal.c \
		env.o objects.o parser.o $(RIPPLE_LIBS)

.PHONY: test-ripple test-ripple-reclamation
test-ripple: ripple test-ripple-reclamation
	@set -e; \
	build_dir=$$(mktemp -d); \
	trap 'rm -rf "$$build_dir"' 0; \
	$(CC) $(CFLAGS) -I. utils/ripple/test.c env.o objects.o parser.o \
		-lm -lutil -o "$$build_dir/test"; \
	"$$build_dir/test"

# Use the real controller and host with a deterministic terminal substitute.
test-ripple-reclamation:
	@set -e; \
	build_dir=$$(mktemp -d); \
	trap 'rm -rf "$$build_dir"' 0; \
	for source in env objects parser; do \
		$(CC) $(CFLAGS) -I. -include test/reclamation.h \
			-Dmalloc=sk_test_alloc -Dfree=sk_test_free \
			-c $$source.c -o "$$build_dir/$$source.o"; \
	done; \
	$(CC) $(CFLAGS) -Dmain=ripple_main -DRIPPLE_HOME='"$(CURDIR)"' \
		-c utils/ripple/ripple.c -o "$$build_dir/ripple.o"; \
	$(CC) $(CFLAGS) -I. utils/ripple/reclamation.c "$$build_dir/"*.o \
		-lm -o "$$build_dir/test"; \
	if ! "$$build_dir/test" > "$$build_dir/result" 2>&1; then \
		cat "$$build_dir/result"; exit 1; \
	fi; \
	tail -n 1 "$$build_dir/result"
