# Native Makefile: GNU Make drives actual BusyBox shell and TinyCC.
.PHONY: all native-c
TCC ?= /bin/tcc
SOURCE ?= /tests/first.c
OBJECT ?= /tmp/first.o

all:
	@echo naseed-make-recipe-one
	@/bin/sh -c 'echo naseed-make-recipe-two'

native-c: all
	@$(TCC) -c $(SOURCE) -o $(OBJECT)
	@/bin/sh -c 'test -s $(OBJECT) && echo naseed-compiled-object'
