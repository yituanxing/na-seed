# Native Makefile: GNU Make drives actual BusyBox shell and TinyCC.
.PHONY: all native-c native-run
TCC ?= /bin/tcc
SOURCE ?= /tests/first.c
OBJECT ?= /tmp/first.o

all:
	@echo naseed-make-recipe-one
	@/bin/sh -c 'echo naseed-make-recipe-two'

native-c: all
	@$(TCC) -c $(SOURCE) -o $(OBJECT)
	@/bin/sh -c 'test -s $(OBJECT) && echo naseed-compiled-object'

# GNU Make builds the executable on the guest, then chmod and execve it
# from the writable tmpfs through the same ELF segment loader as bootfs.
native-run: all
	@/bin/tcc -nostdlib -static -o /tmp/native-hello /tests/native-hello.c
	@/bin/sh -c 'test -s /tmp/native-hello && chmod 755 /tmp/native-hello && /tmp/native-hello'
	@/bin/sh -c '/bin/busybox cp /bin/busybox /tmp/guest-busybox && /bin/busybox chmod 755 /tmp/guest-busybox && /tmp/guest-busybox echo naseed-paged-tmpfs-exec'
