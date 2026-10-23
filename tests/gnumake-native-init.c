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
