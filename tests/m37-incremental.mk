# Genuine Make incremental and -j2 build of upstream zlib: no proxy tools.
# One writable copy of the original /tests/deflate.c allows real source
# modification and exact dependency-driven selective recompile in NaSeed.
CC=/bin/tcc
CFLAGS=-DZ_SOLO -I/tests/include -I/tests
CFILES=adler32 crc32 trees zutil inflate inftrees inffast compress uncompr
OBJECTS=$(addprefix /tmp/,$(addsuffix .o,$(CFILES))) /tmp/deflate.o /tmp/rt.o /tmp/check.o
.PHONY: native-run
define generic_rule
/tmp/$(1).o: /tests/$(1).c
	@/bin/busybox echo M37_OBJECT:$(1).o
	@$(CC) $(CFLAGS) -c $$< -o $$@
endef
$(foreach f,$(CFILES),$(eval $(call generic_rule,$(f))))
/tmp/deflate.c: /tests/deflate.c
	@/bin/busybox cp /tests/deflate.c $@
/tmp/deflate.o: /tmp/deflate.c
	@/bin/busybox echo M37_OBJECT:deflate.o
	@$(CC) $(CFLAGS) -c $< -o $@
/tmp/rt.o: /tests/rt.c
	@/bin/busybox echo M37_OBJECT:rt.o
	@$(CC) $(CFLAGS) -c $< -o $@
/tmp/check.o: /tests/check.c /tests/zlib.h /tests/zconf.h
	@/bin/busybox echo M37_OBJECT:check.o
	@$(CC) $(CFLAGS) -c $< -o $@
/tmp/zlib-check: $(OBJECTS)
	@/bin/busybox echo M37_LINK
	@$(CC) -nostdlib -static -o $@ $^
native-run: /tmp/zlib-check
	@/bin/sh -c 'chmod 755 /tmp/zlib-check && /tmp/zlib-check'
	@/bin/gmake --no-print-directory -q -f /tests/Makefile /tmp/zlib-check
	@/bin/busybox echo M37_UP_TO_DATE
	@/bin/busybox touch /tmp/deflate.c
	@/bin/sh -c '/bin/gmake --no-print-directory -q -f /tests/Makefile /tmp/zlib-check; test $$? -eq 1'
	@/bin/busybox echo M37_DIRTY_DEPENDENCY
	# Recurse through the real GNU Make jobserver instead of forcing
	# -j2 and accidentally recreating an unsupported FIFO jobserver.
	+@$(MAKE) --no-print-directory --jobserver-style=pipe -f /tests/Makefile /tmp/zlib-check
	+@$(MAKE) --no-print-directory -q -f /tests/Makefile /tmp/zlib-check
	# A newly *relinked* ELF has creation mode 0644, not the previous
	# executable inode's mode. Real POSIX chmod must be repeated.
	@/bin/sh -c 'test -s /tmp/deflate.o && chmod 755 /tmp/zlib-check && /tmp/zlib-check'
	@/bin/busybox echo M37_INCREMENTAL_COMPLETE
