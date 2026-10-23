# Two *real* GNU Make recipes: simple POSIX command fast path plus shell.
# This is the same Makefile on native Linux and unmodified NaSeed.
.PHONY: all
all:
	@echo naseed-make-recipe-one
	@/bin/sh -c 'echo naseed-make-recipe-two'
