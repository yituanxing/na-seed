# Real two-translation-unit GNU Make dependency graph.
CC = /usr/bin/gcc
CFLAGS = -O2 -Wall -Wextra -Werror
LDFLAGS = -static
.PHONY: all clean
all: app
app: main.o answer.o
	$(CC) $(LDFLAGS) -o $@ main.o answer.o
main.o: main.c answer.h
	$(CC) $(CFLAGS) -c -o $@ main.c
answer.o: answer.c answer.h
	$(CC) $(CFLAGS) -c -o $@ answer.c
clean:
	rm -f main.o answer.o app
