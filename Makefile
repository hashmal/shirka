CFLAGS+=-std=c99 -pedantic -Wall -Wextra -Wdeclaration-after-statement

shirka: Makefile
shirka: shirka.c shirka.h env.o objects.o parser.o intrinsics.c
	$(CC) $(CFLAGS) -o shirka shirka.c env.o objects.o parser.o

.PHONY: clean test

clean:
	rm -f *.o
	rm -f shirka

test:
	./shirka test/parser.shk
	./shirka test/operations.shk
