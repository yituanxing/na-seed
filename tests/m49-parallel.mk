# Genuine GNU Make -j4: four independent source objects / compiler jobs.
# Each recipe waits until ALL FOUR recipes are running; serialization
# would violate this contract and fail before any success marker appears.
.PHONY: all validate
all: /tmp/m49-alpha /tmp/m49-beta /tmp/m49-gamma /tmp/m49-delta validate
/tmp/m49-alpha:
	@/bin/sh -ec 'touch /tmp/m49-make-alpha-ready; n=0; until test -e /tmp/m49-make-alpha-ready && test -e /tmp/m49-make-beta-ready && test -e /tmp/m49-make-gamma-ready && test -e /tmp/m49-make-delta-ready; do n=$$((n+1)); test $$n -lt 35; /bin/busybox sleep 1; done; echo M49_MAKE_ALPHA_PARALLEL_PASS'
	@/usr/bin/gcc -static /tests/m49-alpha.c -o /tmp/m49-alpha
	@/bin/busybox chmod 755 /tmp/m49-alpha
/tmp/m49-beta:
	@/bin/sh -ec 'touch /tmp/m49-make-beta-ready; n=0; until test -e /tmp/m49-make-alpha-ready && test -e /tmp/m49-make-beta-ready && test -e /tmp/m49-make-gamma-ready && test -e /tmp/m49-make-delta-ready; do n=$$((n+1)); test $$n -lt 35; /bin/busybox sleep 1; done; echo M49_MAKE_BETA_PARALLEL_PASS'
	@/usr/bin/gcc -static /tests/m49-beta.c -o /tmp/m49-beta
	@/bin/busybox chmod 755 /tmp/m49-beta
/tmp/m49-gamma:
	@/bin/sh -ec 'touch /tmp/m49-make-gamma-ready; n=0; until test -e /tmp/m49-make-alpha-ready && test -e /tmp/m49-make-beta-ready && test -e /tmp/m49-make-gamma-ready && test -e /tmp/m49-make-delta-ready; do n=$$((n+1)); test $$n -lt 35; /bin/busybox sleep 1; done; echo M49_MAKE_GAMMA_PARALLEL_PASS'
	@/usr/bin/gcc -static /tests/m49-gamma.c -o /tmp/m49-gamma
	@/bin/busybox chmod 755 /tmp/m49-gamma
/tmp/m49-delta:
	@/bin/sh -ec 'touch /tmp/m49-make-delta-ready; n=0; until test -e /tmp/m49-make-alpha-ready && test -e /tmp/m49-make-beta-ready && test -e /tmp/m49-make-gamma-ready && test -e /tmp/m49-make-delta-ready; do n=$$((n+1)); test $$n -lt 35; /bin/busybox sleep 1; done; echo M49_MAKE_DELTA_PARALLEL_PASS'
	@/usr/bin/gcc -static /tests/m49-delta.c -o /tmp/m49-delta
	@/bin/busybox chmod 755 /tmp/m49-delta

validate: /tmp/m49-alpha /tmp/m49-beta /tmp/m49-gamma /tmp/m49-delta
	@/tmp/m49-alpha
	@/tmp/m49-beta
	@/tmp/m49-gamma
	@/tmp/m49-delta
	@echo M49_GNU_MAKE_J4_PASS
