# Explicit portable POSIX-shell recipes on two independent processes.
# GNU Make is allowed to exec simple commands directly (without shell);
# the initial 'echo' recipe thereby required /bin/echo, which the minimal
# guest intentionally lacked. Call the real shell, not a fake Make stub.
.PHONY: all
all:
	@/bin/sh -c 'echo naseed-make-recipe-one'
	@/bin/sh -c 'echo naseed-make-recipe-two'
