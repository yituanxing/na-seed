/* Linux-side oracle for the original spelling of an execve symlink.
 * The executable inode may be identical to /bin/busybox while AT_EXECFN
 * must still say the exact caller's /bin/cat spelling (including relative
 * variants), independently of argv[0] and the resolved inode path.
 * This is intentionally an actual fork+execve ELF, not a mocked auxv. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void check(bool ok,const char *what) {
    if(!ok) {perror(what);exit(1);}
}
static void run_symlink(const char *directory,const char *path,
                        const char *expected,bool relative) {
    pid_t child=fork();
    check(child>=0,"fork");
    if(!child) {
        if(relative)check(chdir(directory)==0,"chdir");
        char *const arguments[]={
            "different-argv-zero", "--verify-execfn",(char *)expected,NULL
        };
        execv(path,arguments);
        perror("execv");_exit(127);
    }
    int status=0;
    check(waitpid(child,&status,0)==child,"waitpid");
    check(WIFEXITED(status) && WEXITSTATUS(status)==0,"AT_EXECFN oracle child");
}
int main(int argc,char **argv) {
    (void)argv;
    if(argc==3 && !strcmp(argv[1],"--verify-execfn")) {
        const char *original=(const char *)(uintptr_t)getauxval(AT_EXECFN);
        check(original && !strcmp(original,argv[2]),"AT_EXECFN preserves execve spelling");
        check(strcmp(original,"different-argv-zero")!=0,
              "AT_EXECFN independent of argv[0]");
        printf("[EXECFN LINUX CHILD PASS] literal=%s\n",original);
        return 0;
    }
    char real_elf[PATH_MAX];
    ssize_t len=readlink("/proc/self/exe",real_elf,sizeof(real_elf)-1);
    check(len>0 && (size_t)len<sizeof(real_elf),"readlink /proc/self/exe");
    real_elf[len]=0;
    char directory[]="/tmp/naseed-execfn-XXXXXX";
    check(mkdtemp(directory)!=NULL,"mkdtemp");
    char alias[PATH_MAX];
    int count=snprintf(alias,sizeof(alias),"%s/cat",directory);
    check(count>0 && (size_t)count<sizeof(alias),"alias path length");
    check(symlink(real_elf,alias)==0,"symlink to same ELF inode");
    run_symlink(directory,alias,alias,false);
    run_symlink(directory,"./cat","./cat",true);
    check(unlink(alias)==0,"unlink alias");
    check(rmdir(directory)==0,"rmdir temporary directory");
    puts("[EXECFN LINUX ORACLE PASS] original absolute and relative symlink names retained in AT_EXECFN");
    return 0;
}
