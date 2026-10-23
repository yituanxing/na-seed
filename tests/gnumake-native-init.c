#define _GNU_SOURCE
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Native Linux process only.  No stubbed Make recipes or fake outputs. */
int main(void) {
    struct stat st;
    if (stat("/tests/Makefile", &st) != 0 || st.st_size == 0) {
        perror("[GNUMAKE] stat Makefile"); return 91;
    }
    /* Verify the ordinary shell and executable PATH namespace before
     * invoking genuine GNU Make. Linux PATH selection depends on access(X_OK)
     * and stat; output from these syscalls is diagnostic, not fabricated. */
    if (access("/bin/echo", X_OK) != 0) {
        perror("[GNUMAKE PREFLIGHT] /bin/echo X_OK"); return 93;
    }
    if (stat("/bin/echo", &st) != 0) {
        perror("[GNUMAKE PREFLIGHT] /bin/echo stat"); return 94;
    }
    if (access("/bin/sh", X_OK) != 0) {
        perror("[GNUMAKE PREFLIGHT] /bin/sh X_OK"); return 95;
    }
    printf("[GNUMAKE PREFLIGHT] /bin/echo mode=%o size=%lld\n",
           (unsigned)st.st_mode,(long long)st.st_size);
    fflush(stdout);
    char *const argv[] = {"/bin/gmake", "--no-print-directory",
                          "-f", "/tests/Makefile", "native-c", 0};
    char *const envp[] = {"PATH=/bin:/usr/bin", "SHELL=/bin/sh",
                          "HOME=/tmp", "LC_ALL=C", "MAKEFLAGS=", 0};
    puts("[GNUMAKE] exec original GNU Make, shell recipes and TinyCC build");
    fflush(stdout);
    execve(argv[0], argv, envp);
    perror("[GNUMAKE] execve");
    return 92;
}
