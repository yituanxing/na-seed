#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* A normal Linux PID1 (not a game emulator): create controlling serial
 * terminal, check runtime clock and termios, then exec original game ELF. */
int main(void) {
    if (ioctl(0, TIOCSCTTY, 0) != 0) {
        perror("[GAME] TIOCSCTTY"); return 91;
    }
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0) { perror("[GAME] /dev/tty"); return 92; }
    if (dup2(fd, 0) != 0 || dup2(fd, 1) != 1 || dup2(fd, 2) != 2) {
        perror("[GAME] dup2"); return 93;
    }
    if (fd > 2) close(fd);
    struct termios before;
    if (tcgetattr(0, &before) != 0) {
        perror("[GAME] tcgetattr"); return 94;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
        now.tv_sec < 946684800) {
        perror("[GAME] CLOCK_REALTIME"); return 95;
    }
    char *const argv[] = {"/bin/2048", 0};
    char *const envp[] = {"TERM=vt100", "HOME=/tmp", "LC_ALL=C",
                          "PATH=/bin", 0};
    puts("[GAME] launching original terminal 2048");
    fflush(stdout);
    execve(argv[0], argv, envp);
    perror("[GAME] execve");
    return 96;
}
