/* Unmodified Alpine GCC+musl compiles this inside the native NaSeed guest. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static int check_mode(const char *path,mode_t expected) {
    struct stat st;
    return stat(path,&st)==0 && (st.st_mode&07777)==expected;
}
int main(void) {
    mode_t old=umask(0027);
    if (old!=0022) return 10;
    int fd=open("/tmp/m38-mode-file",O_RDWR|O_CREAT|O_EXCL,0777);
    if (fd<0) return 11;
    struct stat st;
    if (fstat(fd,&st) || (st.st_mode&07777)!=0750) return 12;
    if (fchmod(fd,0641) || fstat(fd,&st) ||
        (st.st_mode&07777)!=0641) return 13;
    if (close(fd)) return 14;
    if (mkdir("/tmp/m38-mode-dir",0777)) return 15;
    if (!check_mode("/tmp/m38-mode-dir",0750)) return 16;
    if (umask(0077)!=0027) return 17;
    pid_t child=fork();
    if (child<0) return 18;
    if (!child) {
        if (umask(0)!=0077) _exit(19);
        int f=open("/tmp/m38-mode-child",O_RDWR|O_CREAT|O_EXCL,0666);
        if (f<0) _exit(20);
        if (fstat(f,&st) || (st.st_mode&07777)!=0666) _exit(21);
        if (close(f)) _exit(22);
        _exit(0);
    }
    int status=0;
    if (waitpid(child,&status,0)!=child ||
        !WIFEXITED(status) || WEXITSTATUS(status)!=0) return 23;
    fd=open("/tmp/m38-mode-parent",O_CREAT|O_EXCL|O_RDWR,0666);
    if (fd<0) return 24;
    if (fstat(fd,&st) || (st.st_mode&07777)!=0600) return 25;
    if (close(fd)) return 26;
    if (chmod("/tmp/m38-mode-parent",0755) ||
        !check_mode("/tmp/m38-mode-parent",0755)) return 27;
    if (umask(0022)!=0077) return 28;
    puts("M38_FS_MODE_PASS");
    return 0;
}
