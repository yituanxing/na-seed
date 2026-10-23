# Upstream GNU Make builds and executes an upstream-TinyCC-generated
# Linux ELF. Guest Make, shell, compiler, chmod and execve are all real.
.PHONY: native-run
native-run:
	@/bin/tcc -nostdlib -static -o /tmp/native-hello /tests/native-hello.c
	@/bin/sh -c 'test -s /tmp/native-hello && chmod 755 /tmp/native-hello && /tmp/native-hello'
