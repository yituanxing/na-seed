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
    char *const argv[]={"/bin/nano","-I","/tests/note.txt",NULL};
    char *const envp[]={"TERM=vt100","TERMINFO=/usr/share/terminfo",
        "HOME=/tmp","PATH=/bin:/usr/bin","LC_ALL=C",NULL};
    puts("[NANO] starting genuine GNU nano in native controlling TTY");
    fflush(stdout);
    execve(argv[0],argv,envp);
    perror("[NANO] execve");
    return 94;
}
