/* Same unmodified Linux C filesystem syscalls on real Linux and all three
 * NaSeed ring3 ISAs. Native oracle checks pathname identity, not a mock VFS. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(expr) do { if(!(expr)) { \
    dprintf(2,"[PATH FIRST DIRFD FAIL] line=%d %s errno=%d\n", \
            __LINE__,#expr,errno);return 106; \
} } while(0)
static int failed_errno(int result,int wanted) {
    return result==-1 && errno==wanted;
}
int main(void) {
    char base[128];
    CHECK(snprintf(base,sizeof(base),"/tmp/path-first-dirfd-%ld",
                   (long)getpid())>0);
    errno=0;
    CHECK(failed_errno(mkdir("/",0700),EEXIST));
    CHECK(mkdir(base,0700)==0);
    int root=open(base,O_RDONLY|O_DIRECTORY);
    CHECK(root>=0);
    CHECK(mkdirat(root,"old",0700)==0);
    int dir=openat(root,"old",O_RDONLY|O_DIRECTORY);
    CHECK(dir>=0);
    int pathfd=openat(root,"old",O_PATH|O_DIRECTORY);
    CHECK(pathfd>=0);
    CHECK(renameat(root,"old",root,"moved")==0);
    CHECK(mkdirat(root,"old",0700)==0);
    int child=openat(dir,"child",O_CREAT|O_EXCL|O_RDWR,0600);
    CHECK(child>=0);
    CHECK(write(child,"path-owned",10)==10);
    struct stat st;
    CHECK(fstatat(root,"moved/child",&st,0)==0 && S_ISREG(st.st_mode));
    errno=0;
    CHECK(failed_errno(fstatat(root,"old/child",&st,0),ENOENT));
    int via_path=openat(pathfd,"opath",O_CREAT|O_EXCL|O_WRONLY,0600);
    CHECK(via_path>=0 && close(via_path)==0);
    CHECK(fstatat(root,"moved/opath",&st,0)==0);
    CHECK(fchdir(dir)==0);
    char current[256];
    CHECK(getcwd(current,sizeof(current))!=NULL);
    CHECK(strstr(current,"/moved")!=NULL);
    CHECK(fchdir(root)==0);
    CHECK(symlinkat("moved/child",root,"sym")==0);
    CHECK(linkat(root,"sym",root,"hard",AT_SYMLINK_FOLLOW)==0);
    CHECK(fstatat(root,"hard",&st,AT_SYMLINK_NOFOLLOW)==0);
    CHECK(S_ISREG(st.st_mode));
    errno=0;
    CHECK(failed_errno(openat(root,"moved/child/..",O_RDONLY),ENOTDIR));
    CHECK(unlinkat(root,"sym",0)==0);
    CHECK(unlinkat(root,"hard",0)==0);
    CHECK(close(child)==0);
    CHECK(unlinkat(dir,"child",0)==0);
    CHECK(unlinkat(pathfd,"opath",0)==0);
    CHECK(unlinkat(root,"moved",AT_REMOVEDIR)==0);
    /* Linux permits a disconnected directory as cwd, but cannot return
     * its old spelling or create files in a deleted parent. */
    CHECK(fchdir(dir)==0);
    errno=0;
    CHECK(getcwd(current,sizeof(current))==NULL && errno==ENOENT);
    errno=0;
    CHECK(failed_errno(openat(dir,"ghost",O_CREAT|O_EXCL|O_WRONLY,0600),ENOENT));
    CHECK(fchdir(root)==0);
    errno=0;
    CHECK(failed_errno(fstatat(root,"old/ghost",&st,0),ENOENT));
    CHECK(close(pathfd)==0 && close(dir)==0);
    CHECK(unlinkat(root,"old",AT_REMOVEDIR)==0);
    int ignored=openat(-99,"/tmp",O_RDONLY|O_DIRECTORY);
    CHECK(ignored>=0 && close(ignored)==0);
    CHECK(close(root)==0);
    CHECK(rmdir(base)==0);
    puts("PATH_FIRST_DIRFD_NATIVE_PASS");
    return 0;
}
