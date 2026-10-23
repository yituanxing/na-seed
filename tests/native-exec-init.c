#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
/* Native PID1: ensure CPIO remains immutable then exec original GNU Make.
 * The actual generated file is created in /tmp, NOT included in the CPIO. */
int main(void) {
    struct stat st;
    if(stat("/tests/Makefile",&st) || st.st_size<=0) {
        perror("[NATIVE-EXEC] original Makefile");return 91;
    }
    errno=0;
    if(chmod("/tests/Makefile",0755)==0 || errno!=EROFS) {
        fprintf(stderr,"[NATIVE-EXEC] CPIO chmod did not report EROFS: %d\n",errno);
        return 92;
    }
    char *const argv[]={"/bin/gmake","--no-print-directory",
                         "-f","/tests/Makefile","native-run",0};
    char *const envp[]={"PATH=/bin:/usr/bin","SHELL=/bin/sh",
                         "HOME=/tmp","LC_ALL=C","MAKEFLAGS=",0};
    puts("[NATIVE-EXEC] GNU Make builds, chmods and runs a TinyCC ELF");
    fflush(stdout);
    execve(argv[0],argv,envp);
    perror("[NATIVE-EXEC] GNU Make execve");
    return 93;
}
