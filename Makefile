.SUFFIXES: .c .o

shirka: shirka.c shirka.h env.o objects.o parser.o intrinsics.c
	$(CC) -std=c99 -pedantic -Wall -Wextra -Wdeclaration-after-statement \
	-o shirka shirka.c env.o objects.o parser.o

clean:
	rm *.o

.c.o:
	$(CC) -std=c99 -pedantic -Wall -Wextra -Wdeclaration-after-statement \
	-o $@ -c $<
