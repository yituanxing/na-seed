#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

/* Minimal native *mount plumbing*, not a substitute init or service manager.
 * The ORIGINAL Debian SysVinit PID1 processes SIGUSR1 and creates the actual
 * /run/initctl FIFO inside the newly mounted NaSeed tmpfs superblock. */
int main(void) {
    struct stat st;
    if (stat("/run", &st) || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[M256 MOUNT FAIL] /run: %s\n", strerror(errno));
        return 94;
    }
    if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV,
              "mode=0755,size=4M,nr_inodes=1024")) {
        fprintf(stderr, "[M256 MOUNT FAIL] tmpfs /run: %s\n", strerror(errno));
        return 95;
    }
    puts("[M256 RUN TMPFS] real mount(2) attached /run");
    fflush(stdout);
    if (kill(1, SIGUSR1)) {
        fprintf(stderr, "[M256 MOUNT FAIL] SIGUSR1 original init: %s\n", strerror(errno));
        return 96;
    }
    puts("[M256 INITCTL] signaled real Debian PID1 to reopen control FIFO");
    fflush(stdout);
    return 0;
}
