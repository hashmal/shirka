.SUFFIXES: .c .o

shirka: shirka.c shirka.h env.o objects.o parser.o
	gcc -ansi -pedantic -Wall -o shirka shirka.c env.o objects.o parser.o

clean:
	rm *.o

.c.o:
	gcc -ansi -pedantic -Wall -c $< -o $@
