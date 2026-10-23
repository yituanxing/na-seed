# Full multi-object original upstream application built by real GNU Make
# and original native TinyCC INSIDE NaSeed, without patching aes.c/aes.h.
# Linux's guest file stat, open/read/write, fork/wait, timestamps and
# executable loader decide if a rebuild is necessary.
CC=/bin/tcc
CPPFLAGS=-I/tests/include -I/tests
COMPILE=$(CC) $(CPPFLAGS) -c
.PHONY: native-run
/tmp/aes.o: /tests/aes.c /tests/aes.h
	@$(COMPILE) /tests/aes.c -o $@
/tmp/rt.o: /tests/rt.c
	@$(COMPILE) /tests/rt.c -o $@
/tmp/check.o: /tests/check.c /tests/aes.h
	@$(COMPILE) /tests/check.c -o $@
/tmp/aes-check: /tmp/aes.o /tmp/rt.o /tmp/check.o
	@$(CC) -nostdlib -static -o $@ $^
native-run: /tmp/aes-check
	@/bin/sh -c 'test -s /tmp/aes.o && test -s /tmp/rt.o && test -s /tmp/check.o && test -s /tmp/aes-check && chmod 755 /tmp/aes-check && /tmp/aes-check'
