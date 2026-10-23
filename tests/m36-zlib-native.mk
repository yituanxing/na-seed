# An authentic upstream zlib multi-source build. All object files and the
# executable are created *inside NaSeed* by unmodified GNU Make and TinyCC.
# The original library source is never patched or linked on the host here.
CC=/bin/tcc
CFLAGS=-DZ_SOLO -I/tests
CFILES=adler32 crc32 deflate trees zutil inflate inftrees inffast compress uncompr
OBJECTS=$(addprefix /tmp/,$(addsuffix .o,$(CFILES))) /tmp/rt.o /tmp/check.o
.PHONY: native-run
define zlib_rule
/tmp/$(1).o: /tests/$(1).c
	@$(CC) $(CFLAGS) -c $$< -o $$@
endef
$(foreach f,$(CFILES),$(eval $(call zlib_rule,$(f))))
/tmp/rt.o: /tests/rt.c
	@$(CC) $(CFLAGS) -c $< -o $@
/tmp/check.o: /tests/check.c /tests/zlib.h /tests/zconf.h
	@$(CC) $(CFLAGS) -c $< -o $@
/tmp/zlib-check: $(OBJECTS)
	@$(CC) -nostdlib -static -o $@ $^
native-run: /tmp/zlib-check
	@/bin/sh -c 'test -s /tmp/adler32.o && test -s /tmp/crc32.o && test -s /tmp/deflate.o && test -s /tmp/trees.o && test -s /tmp/zutil.o && test -s /tmp/inflate.o && test -s /tmp/inftrees.o && test -s /tmp/inffast.o && test -s /tmp/compress.o && test -s /tmp/uncompr.o && test -s /tmp/rt.o && test -s /tmp/check.o && chmod 755 /tmp/zlib-check && /tmp/zlib-check'
