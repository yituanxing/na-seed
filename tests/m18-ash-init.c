#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
/* Genuine native C PID1 transport launcher, NOT a fake shell or parser:
 * attach the existing tty to this session, give all three stdio streams
 * real TTY descriptors, then replace ourselves with unmodified BusyBox ash.
 * No NaSeed-specific syscall numbers or startup hooks in the program.
 */
static void die(const char *msg) {
    fprintf(stderr, "[M18] %s errno=%d\n", msg, errno);
    _exit(93);
}
static void oracle(void) {
    struct termios t;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &t) != 0)
        die("host needs real controlling PTY");
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0 || tcgetattr(fd, &t) != 0) die("host PTY open");
    close(fd);
    puts("m18-linux-pty-ok");
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--linux-pty-oracle") == 0) {
        oracle();
        return 0;
    }
    if (argc != 1) return 92;
    if (ioctl(STDIN_FILENO, TIOCSCTTY, 0) != 0)
        die("attach real console to PID1 session");
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0) die("open genuine /dev/tty");
    if (dup2(fd, STDIN_FILENO) != 0 ||
        dup2(fd, STDOUT_FILENO) != 1 ||
        dup2(fd, STDERR_FILENO) != 2)
        die("dup terminal onto stdio");
    if (fd > STDERR_FILENO) close(fd);
    if (!isatty(0) || !isatty(1) || !isatty(2)) die("stdio not a TTY");
    struct termios t;
    if (tcgetattr(0, &t) != 0) die("read tty termios");
    t.c_lflag |= ICANON | ECHO;
    t.c_iflag |= ICRNL;
    if (tcsetattr(0, TCSANOW, &t) != 0) die("set native tty line mode");
    printf("[M18] launching original BusyBox ash on real TTY\n");
    fflush(stdout);
    char *const shell_argv[] = {"/bin/busybox", "sh", "-i", NULL};
    char *const shell_env[] = {
        "HOME=/", "PATH=/bin:/usr/bin", "TERM=dumb", "PS1=M18> ",
        "LC_ALL=C", NULL
    };
    execve(shell_argv[0], shell_argv, shell_env);
    die("execve real source BusyBox ash");
    return 93;
}
