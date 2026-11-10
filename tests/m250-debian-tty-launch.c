#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* M250's explicitly test-built, static transport launcher lives on physical
 * ext4. It does not contain a shell, a parser, a glibc shim, or any distro
 * bytes. After acquiring the genuine UART controlling terminal it execve()s
 * the unmodified, signed Debian 13 PT_INTERP /usr/bin/dash in this same PID1.
 */
static void fail(const char *operation) {
    fprintf(stderr, "[M250 LAUNCH FAIL] %s: errno=%d (%s)\n",
            operation, errno, strerror(errno));
    _exit(93);
}

int main(int argc, char **argv) {
    /* Explicit non-empty manifest argument avoids shell IFS collapsing empty
     * TSV columns in the existing shared native-QEMU parity runner. */
    if (argc != 2 || strcmp(argv[1], "--console") != 0) return 92;

    if (ioctl(STDIN_FILENO, TIOCSCTTY, 0) != 0)
        fail("TIOCSCTTY on kernel-provided UART");
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0) fail("open controlling /dev/tty");
    if (dup2(fd, STDIN_FILENO) < 0 ||
        dup2(fd, STDOUT_FILENO) < 0 ||
        dup2(fd, STDERR_FILENO) < 0)
        fail("dup2 controlling TTY onto stdin/out/err");
    if (fd > STDERR_FILENO) close(fd);
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
        !isatty(STDERR_FILENO))
        fail("stdio is not the same real controlling terminal");

    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) != 0)
        fail("TCGETS original termios");
    term.c_lflag |= ICANON | ECHO;
    term.c_iflag |= ICRNL;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) != 0)
        fail("TCSETS native canonical/ECHO/ICRNL");

    puts("[M250 CTTY] real /dev/tty acquired; replacing same PID1 with signed Debian dash -i");
    fflush(stdout);
    char *const dash_argv[] = {"/usr/bin/dash", "-i", NULL};
    char *const dash_env[] = {
        "HOME=/", "PATH=/usr/bin:/bin", "TERM=dumb", "PS1=M250> ",
        "LC_ALL=C", NULL
    };
    execve(dash_argv[0], dash_argv, dash_env);
    fail("execve original signed Debian dash PT_INTERP");
    return 93;
}
