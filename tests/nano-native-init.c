#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
/* Native Linux C PID1 creates an actual controlling terminal, then execs
 * the untouched GNU nano ELF. No hand-crafted editor or kernel shortcut. */
int main(void) {
    if (ioctl(0, TIOCSCTTY, 0) != 0) {
        perror("[NANO] TIOCSCTTY"); return 90;
    }
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0) { perror("[NANO] /dev/tty"); return 91; }
    if (dup2(fd, 0) != 0 || dup2(fd, 1) != 1 || dup2(fd, 2) != 2) {
        perror("[NANO] dup2"); return 92;
    }
    if (fd > 2) close(fd);
    struct termios t;
    if (tcgetattr(0, &t) != 0) { perror("[NANO] tcgetattr"); return 93; }
    /* Probe the same real namespace/terminfo nano will use.  Errors are
     * surfaced before exec, not bypassed by a patched editor. */
    if (access("/usr/share/terminfo/v/vt100", R_OK) != 0) {
        perror("[NANO] terminfo access"); return 95;
    }
    if (access("/tests/note.txt", R_OK) != 0 ||
        access("/tests/missing.txt", F_OK) == 0) {
        perror("[NANO] namespace access"); return 96;
    }
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) != 0 || ws.ws_row == 0 || ws.ws_col == 0) {
        perror("[NANO] TIOCGWINSZ"); return 97;
    }
    char *const argv[]={"/bin/nano","/tests/note.txt",NULL};
    char *const envp[]={"TERM=vt100","TERMINFO=/usr/share/terminfo",
        "HOME=/tmp","PATH=/bin:/usr/bin","LC_ALL=C",NULL};
    puts("[NANO] starting genuine GNU nano in native controlling TTY");
    fflush(stdout);
    execve(argv[0],argv,envp);
    perror("[NANO] execve");
    return 94;
}
