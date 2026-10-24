# Two independent *real GCC* jobs run under ORIGINAL GNU Make -j2.
# A two-file rendezvous proves both recipes are live simultaneously.
# Do not replace real user program with a host tool or synthetic kernel API.
.PHONY: all assert
all: /tmp/m48-alpha /tmp/m48-beta assert
/tmp/m48-alpha:
	@/bin/sh -ec 'touch /tmp/m48-compile-a-start; n=0; until test -e /tmp/m48-compile-b-start; do n=$$((n+1)); test $$n -lt 25; /bin/busybox sleep 1; done; echo M48_MAKE_PARALLEL_A_PASS'
	@/usr/bin/gcc -static /tests/m48-alpha.c -o /tmp/m48-alpha
	@/bin/busybox chmod 755 /tmp/m48-alpha
/tmp/m48-beta:
	@/bin/sh -ec 'touch /tmp/m48-compile-b-start; n=0; until test -e /tmp/m48-compile-a-start; do n=$$((n+1)); test $$n -lt 25; /bin/busybox sleep 1; done; echo M48_MAKE_PARALLEL_B_PASS'
	@/usr/bin/gcc -static /tests/m48-beta.c -o /tmp/m48-beta
	@/bin/busybox chmod 755 /tmp/m48-beta
assert: /tmp/m48-alpha /tmp/m48-beta
	@/tmp/m48-alpha
	@/tmp/m48-beta
	@echo M48_MAKE_GCC_BUILD_PASS
