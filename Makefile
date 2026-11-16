CFLAGS+=-std=c99 -pedantic -Wall -Wextra -Wdeclaration-after-statement

shirka: Makefile
shirka: shirka.c shirka.h env.o objects.o parser.o intrinsics.c
	$(CC) $(CFLAGS) -o shirka shirka.c env.o objects.o parser.o

.PHONY: clean test test-reclamation

clean:
	rm -f *.o
	rm -f shirka

test:
	./shirka test/parser.shk
	./shirka test/operations.shk

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
