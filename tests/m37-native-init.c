/* Original glibc/musl target-native Linux ELF init.
 * Hand GNU Make its real -j2 parallel build mode, not a fake serial loop.
 * The guest Makefile validates no-op incremental and one-target rebuild. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
    struct stat st;
    if(stat("/tests/Makefile",&st) || st.st_size<=0) return 91;
    errno=0;
    if(chmod("/tests/Makefile",0755)==0 || errno!=EROFS) return 92;
    char *const argv[]={"/bin/gmake","--no-print-directory",
                         "--jobs=2","-f","/tests/Makefile","native-run",0};
    char *const envp[]={"PATH=/bin:/usr/bin","SHELL=/bin/sh",
                         "HOME=/tmp","LC_ALL=C","MAKEFLAGS=",0};
    puts("[M37] upstream zlib parallel GNU Make -j2 incremental rebuild");
    fflush(stdout);
    execve(argv[0],argv,envp);
    perror("[M37] GNU Make execve");
    return 93;
}
