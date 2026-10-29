/* Linux's actual VFS semantics for NaSeed Path-first migration.
 * This is a host oracle, NOT a NaSeed guest acceptance. No privileges or
 * mount(2) calls are required. Each test owns a unique temporary directory.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"[PATH FIRST LINUX FAIL] %s:%d: %s (errno=%d %s)\n", \
            __FILE__,__LINE__,#expr,errno,strerror(errno)); exit(1); \
} } while (0)

static void check_errno(int result,int wanted) {
    CHECK(result == -1);
    CHECK(errno == wanted);
}

static void unchanged_open_file(int rootfd) {
    static const char old[]="old-data", fresh[]="new-data";
    int oldfd=openat(rootfd,"live",O_RDWR|O_CREAT|O_EXCL,0600);
    CHECK(oldfd>=0);
    CHECK(write(oldfd,old,sizeof(old)-1)==(ssize_t)sizeof(old)-1);
    struct stat before;
    CHECK(fstat(oldfd,&before)==0);
    CHECK(unlinkat(rootfd,"live",0)==0);
    struct stat after;
    CHECK(fstat(oldfd,&after)==0);
    CHECK(after.st_ino==before.st_ino && after.st_nlink==0);
    int newfd=openat(rootfd,"live",O_RDWR|O_CREAT|O_EXCL,0600);
    CHECK(newfd>=0);
    CHECK(write(newfd,fresh,sizeof(fresh)-1)==(ssize_t)sizeof(fresh)-1);
    struct stat newstat;
    CHECK(fstat(newfd,&newstat)==0 && newstat.st_ino!=after.st_ino);
    char buf[32]={0};
    CHECK(pread(oldfd,buf,sizeof(old)-1,0)==(ssize_t)sizeof(old)-1);
    CHECK(!memcmp(buf,old,sizeof(old)-1));
    CHECK(pread(newfd,buf,sizeof(fresh)-1,0)==(ssize_t)sizeof(fresh)-1);
    CHECK(!memcmp(buf,fresh,sizeof(fresh)-1));
    CHECK(close(oldfd)==0 && close(newfd)==0);
    CHECK(unlinkat(rootfd,"live",0)==0);
    puts("[PATH FIRST LINUX PASS] open-unlink identity and inode replacement");
}

static void renamed_and_unlinked_dirfd(int rootfd) {
    CHECK(mkdirat(rootfd,"old",0700)==0);
    int dirfd=openat(rootfd,"old",O_RDONLY|O_DIRECTORY);
    CHECK(dirfd>=0);
    CHECK(renameat(rootfd,"old",rootfd,"moved")==0);
    /* Recreating the old pathname must not redirect an already-open FD. */
    CHECK(mkdirat(rootfd,"old",0700)==0);
    int child=openat(dirfd,"child",O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(child>=0 && close(child)==0);
    struct stat st;
    CHECK(fstatat(rootfd,"moved/child",&st,0)==0);
    errno=0;
    check_errno(fstatat(rootfd,"old/child",&st,0),ENOENT);
    CHECK(fchdir(dirfd)==0);
    char cwd[PATH_MAX];
    CHECK(getcwd(cwd,sizeof(cwd))!=NULL);
    CHECK(strstr(cwd,"/moved")!=NULL);
    CHECK(unlinkat(dirfd,"child",0)==0);
    /* Linux permits rmdir of a directory that IS the process cwd.
     * Its inode remains owned by cwd+dirfd, but its old name disappears;
     * a historical cwd pathname cache cannot veto rmdir with EBUSY. */
    CHECK(unlinkat(rootfd,"moved",AT_REMOVEDIR)==0);
    errno=0;
    CHECK(getcwd(cwd,sizeof(cwd))==NULL && errno==ENOENT);
    errno=0;
    check_errno(openat(dirfd,"after",O_WRONLY|O_CREAT|O_EXCL,0600),ENOENT);
    CHECK(fchdir(rootfd)==0);
    CHECK(close(dirfd)==0);
    CHECK(unlinkat(rootfd,"old",AT_REMOVEDIR)==0);
    puts("[PATH FIRST LINUX PASS] renamed dirfd stays inode-bound; unlinked cwd fails closed");
}

static void symlink_and_trailing_slash(int rootfd) {
    int file=openat(rootfd,"plain",O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(file>=0 && close(file)==0);
    CHECK(mkdirat(rootfd,"dir",0700)==0);
    CHECK(symlinkat("plain",rootfd,"plain-link")==0);
    CHECK(symlinkat("dir",rootfd,"dir-link")==0);
    errno=0;
    check_errno(openat(rootfd,"plain/.",O_RDONLY),ENOTDIR);
    errno=0;
    check_errno(openat(rootfd,"plain/..",O_RDONLY),ENOTDIR);
    errno=0;
    check_errno(openat(rootfd,"plain-link/",O_RDONLY),ENOTDIR);
    int linked=openat(rootfd,"dir-link/",O_RDONLY|O_DIRECTORY);
    CHECK(linked>=0 && close(linked)==0);
    /* Mutating operations cannot discard a trailing '/' and accidentally
     * unlink or rename a REGULAR file. mkdirat("directory/") is different:
     * Linux intentionally allows creating a directory with that spelling. */
    errno=0;
    check_errno(unlinkat(rootfd,"plain/",0),ENOTDIR);
    errno=0;
    check_errno(renameat(rootfd,"plain/",rootfd,"renamed-plain"),ENOTDIR);
    errno=0;
    check_errno(linkat(rootfd,"plain/",rootfd,"linked-plain",0),ENOTDIR);
    errno=0;
    check_errno(linkat(rootfd,"plain",rootfd,"alias/",0),ENOENT);
    /* A symlink-to-directory is traversable by open(dir-link/) but must
     * NOT be unlinked or renamed by silently stripping the final slash. */
    errno=0;
    check_errno(unlinkat(rootfd,"dir-link/",0),ENOTDIR);
    errno=0;
    check_errno(unlinkat(rootfd,"dir-link/",AT_REMOVEDIR),ENOTDIR);
    errno=0;
    check_errno(renameat(rootfd,"dir-link/",rootfd,"renamed-link"),ENOTDIR);
    /* mkdir has different final-component rules from unlink/rename:
     * existing regular files, directories and symlinks are EEXIST. */
    errno=0;
    check_errno(mkdirat(rootfd,"plain/",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"dir/",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"plain-link/",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"dir-link/",0700),EEXIST);
    /* GNU tar/BusyBox extraction commonly attempts mkdir(".").
     * Linux namei treats terminal dot/dotdot as existing directories,
     * rather than invalid basenames in namespace creation. */
    errno=0;
    check_errno(mkdirat(rootfd,".",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"..",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"./",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"/",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"dir/.",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"dir/..",0700),EEXIST);
    errno=0;
    check_errno(mkdirat(rootfd,"missing/.",0700),ENOENT);
    errno=0;
    check_errno(mkdirat(rootfd,"plain/.",0700),ENOTDIR);
    CHECK(mkdirat(rootfd,"new-dir/",0700)==0);
    CHECK(unlinkat(rootfd,"new-dir",AT_REMOVEDIR)==0);
    struct stat original;
    CHECK(fstatat(rootfd,"plain",&original,0)==0 && S_ISREG(original.st_mode));
    errno=0;
    check_errno(fstatat(rootfd,"renamed-plain",&original,0),ENOENT);
    errno=0;
    check_errno(fstatat(rootfd,"linked-plain",&original,0),ENOENT);
    errno=0;
    check_errno(fstatat(rootfd,"alias",&original,0),ENOENT);
    CHECK(symlinkat("created-target",rootfd,"dangling")==0);
    errno=0;
    check_errno(mkdirat(rootfd,"dangling/",0700),EEXIST);
    errno=0;
    check_errno(symlinkat("anything",rootfd,"plain/"),EEXIST);
    errno=0;
    check_errno(symlinkat("anything",rootfd,"dir/"),EEXIST);
    errno=0;
    check_errno(symlinkat("anything",rootfd,"missing-symlink/"),ENOENT);
    errno=0;
    check_errno(linkat(rootfd,"plain",rootfd,"plain/",0),EEXIST);
    errno=0;
    check_errno(linkat(rootfd,"plain",rootfd,"dir/",0),EEXIST);
    errno=0;
    check_errno(linkat(rootfd,"plain",rootfd,"missing-hardlink/",0),ENOENT);
    errno=0;
    check_errno(renameat(rootfd,"plain",rootfd,"missing-rename/"),ENOTDIR);
    errno=0;
    check_errno(renameat(rootfd,"plain",rootfd,"dir/"),ENOTDIR);
    CHECK(mkdirat(rootfd,"dir-old",0700)==0);
    CHECK(mkdirat(rootfd,"dir-new",0700)==0);
    CHECK(renameat(rootfd,"dir-old",rootfd,"dir-new/")==0);
    errno=0;
    struct stat moved_stat;
    check_errno(fstatat(rootfd,"dir-old",&moved_stat,0),ENOENT);
    CHECK(unlinkat(rootfd,"dir-new",AT_REMOVEDIR)==0);
    errno=0;
    check_errno(openat(rootfd,"dangling",O_CREAT|O_EXCL|O_WRONLY,0600),EEXIST);
    int followed=openat(rootfd,"dangling",O_CREAT|O_WRONLY,0600);
    CHECK(followed>=0 && close(followed)==0);
    struct stat st;
    CHECK(fstatat(rootfd,"created-target",&st,0)==0 && S_ISREG(st.st_mode));
    CHECK(unlinkat(rootfd,"created-target",0)==0);
    CHECK(unlinkat(rootfd,"dangling",0)==0);
    CHECK(unlinkat(rootfd,"plain-link",0)==0);
    CHECK(unlinkat(rootfd,"dir-link",0)==0);
    CHECK(unlinkat(rootfd,"plain",0)==0);
    CHECK(unlinkat(rootfd,"dir",AT_REMOVEDIR)==0);
    puts("[PATH FIRST LINUX PASS] symlink follow/O_EXCL and trailing slash/ENOTDIR");
}

static void long_path_over_255(int rootfd,const char *absolute_root) {
    static const char component[]="longcomponent";
    char relative[PATH_MAX]={0},absolute[PATH_MAX];
    int parentfd=dup(rootfd);
    CHECK(parentfd>=0);
    for(unsigned i=0;i<32;i++) {
        CHECK(mkdirat(parentfd,component,0700)==0);
        int next=openat(parentfd,component,O_RDONLY|O_DIRECTORY);
        CHECK(next>=0 && close(parentfd)==0);
        parentfd=next;
        size_t n=strlen(relative);
        CHECK(n+sizeof(component)+1<sizeof(relative));
        if(n)relative[n++]='/';
        memcpy(relative+n,component,sizeof(component));
    }
    int file=openat(parentfd,"leaf",O_CREAT|O_EXCL|O_WRONLY,0600);
    CHECK(file>=0 && close(file)==0);
    CHECK(strlen(absolute_root)+strlen(relative)>255);
    CHECK(snprintf(absolute,sizeof(absolute),"%s/%s/leaf",absolute_root,relative)>255);
    file=open(absolute,O_RDONLY);
    CHECK(file>=0 && close(file)==0);
    CHECK(unlinkat(parentfd,"leaf",0)==0);
    CHECK(close(parentfd)==0);
    for(unsigned i=0;i<32;i++) {
        CHECK(unlinkat(rootfd,relative,AT_REMOVEDIR)==0);
        char *slash=strrchr(relative,'/');
        if(slash)*slash=0;
    }
    puts("[PATH FIRST LINUX PASS] valid filesystem path longer than 255 bytes");
}

static void real_mount_crossing(void) {
    int rootfd=open("/",O_RDONLY|O_DIRECTORY);
    int procfd=open("/proc",O_RDONLY|O_DIRECTORY);
    CHECK(rootfd>=0);
    if(procfd<0) {
        puts("[PATH FIRST LINUX SKIP] /proc mount unavailable");
        CHECK(close(rootfd)==0);
        return;
    }
    struct stat root,proc,parent;
    CHECK(fstat(rootfd,&root)==0 && fstat(procfd,&proc)==0);
    if(root.st_dev==proc.st_dev) {
        puts("[PATH FIRST LINUX SKIP] /proc is not an independent mounted filesystem");
    } else {
        int parentfd=openat(procfd,"..",O_RDONLY|O_DIRECTORY);
        CHECK(parentfd>=0 && fstat(parentfd,&parent)==0);
        CHECK(parent.st_dev==root.st_dev && parent.st_ino==root.st_ino);
        CHECK(close(parentfd)==0);
        puts("[PATH FIRST LINUX PASS] real /proc mount crossing and .. escape");
    }
    CHECK(close(procfd)==0 && close(rootfd)==0);
}

int main(void) {
    char directory[]="/tmp/naseed-path-first-XXXXXX";
    CHECK(mkdtemp(directory)!=NULL);
    int rootfd=open(directory,O_RDONLY|O_DIRECTORY);
    CHECK(rootfd>=0);
    unchanged_open_file(rootfd);
    renamed_and_unlinked_dirfd(rootfd);
    symlink_and_trailing_slash(rootfd);
    long_path_over_255(rootfd,directory);
    real_mount_crossing();
    CHECK(close(rootfd)==0);
    CHECK(rmdir(directory)==0);
    puts("[PATH FIRST LINUX ORACLE PASS] all mandatory host Linux namespace tests");
    return 0;
}
