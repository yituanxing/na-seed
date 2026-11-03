#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

/* A normal, independently linked Linux ELF proves that /dev is a mounted
 * device filesystem and that char-special FileOps work after VFS namei.
 * It then execs the existing real GUI, with no kernel-specific test syscall
 * or framebuffer markers synthesized by the host runner. */
static void require(int ok,const char *step) {
    if(ok)return;
    fprintf(stderr,"[M174 DEVFS FAIL] %s errno=%d\n",step,errno);
    fflush(stderr);
    exit(74);
}
static int directory_has(const char *path,const char *name) {
    DIR *d=opendir(path);
    require(d!=NULL,"opendir live devfs directory");
    int found=0;
    struct dirent *entry;
    errno=0;
    while((entry=readdir(d))!=NULL) {
        if(!strcmp(entry->d_name,name))found=1;
        errno=0;
    }
    require(errno==0,"getdents from live devfs directory");
    require(closedir(d)==0,"close live devfs directory");
    return found;
}
static void compare_inode(int fd,const char *path,unsigned major_id,
                          unsigned minor_id) {
    struct stat from_path={0},from_file={0};
    require(stat(path,&from_path)==0,"stat devfs node by pathname");
    require(fstat(fd,&from_file)==0,"fstat pinned devfs OpenFile");
    require(S_ISCHR(from_path.st_mode) && S_ISCHR(from_file.st_mode),
            "mounted devfs node has S_IFCHR");
    require(from_path.st_dev==from_file.st_dev &&
            from_path.st_ino==from_file.st_ino &&
            from_path.st_rdev==from_file.st_rdev,
            "VFS stat and fstat preserve same inode identity");
    require(major(from_file.st_rdev)==major_id &&
            minor(from_file.st_rdev)==minor_id,
            "devfs node reports Linux major/minor");
}
static void exercise_pseudo_devices(void) {
    require(directory_has("/dev","null") &&
            directory_has("/dev","zero") &&
            directory_has("/dev","random") &&
            directory_has("/dev","urandom") &&
            directory_has("/dev","pts") &&
            directory_has("/dev","input"),
            "devfs readdir publishes files and child directories");
    int parent=open("/dev",O_RDONLY|O_DIRECTORY);
    require(parent>=0,"open /dev directory");
    int fd=openat(parent,"null",O_RDWR|O_TRUNC);
    require(fd>=0,"openat character inode O_RDWR|O_TRUNC");
    compare_inode(fd,"/dev/null",1,3);
    const int alias=dup(fd);
    require(alias>=0,"dup devfs OpenFile");
    require(close(fd)==0,"close first devfs descriptor");
    require(write(alias,"ABC",3)==3,"null writable after dup");
    require(read(alias,(char[1]){0},1)==0,"null EOF after dup");
    require(close(alias)==0,"final close of devfs OpenFile");
    fd=openat(parent,"zero",O_RDONLY);
    require(fd>=0,"openat /dev/zero");
    compare_inode(fd,"/dev/zero",1,5);
    uint8_t zeroes[32]={1};
    require(read(fd,zeroes,sizeof(zeroes))==(ssize_t)sizeof(zeroes),
            "zero provider reads full count");
    for(unsigned i=0;i<sizeof(zeroes);i++)
        require(!zeroes[i],"/dev/zero bytes are zero");
    require(close(fd)==0 && close(parent)==0,"release VFS dirfd and zero");
    errno=0;
    require(open("/dev/null",O_CREAT|O_EXCL|O_WRONLY,0600)==-1 &&
            errno==EEXIST,"O_EXCL cannot replace existing device inode");
    fd=open("/dev/null",O_PATH);
    require(fd>=0,"O_PATH pins devfs inode without device open");
    compare_inode(fd,"/dev/null",1,3);
    errno=0;
    require(read(fd,zeroes,1)==-1 && errno==EBADF,
            "O_PATH cannot read character device");
    require(close(fd)==0,"release O_PATH device inode");
    puts("M174_DEVFS_PSEUDO_PASS");fflush(stdout);
}
static void exercise_hardware_and_pty(void) {
    require(directory_has("/dev","fb0"),"display backend publishes fb0");
    int fd=open("/dev/fb0",O_RDWR);
    require(fd>=0,"open fb0 through mounted devfs");
    compare_inode(fd,"/dev/fb0",29,0);
    struct fb_var_screeninfo v={0};
    require(ioctl(fd,FBIOGET_VSCREENINFO,&v)==0 &&
            v.xres==800 && v.yres==600 && v.bits_per_pixel==32,
            "real fbdev ioctl through VFS-mounted char FileOps");
    require(close(fd)==0,"close framebuffer device");
    require(directory_has("/dev/input","event0"),
            "Linux input owner publishes event0");
    fd=open("/dev/input/event0",O_RDONLY|O_NONBLOCK);
    require(fd>=0,"open input event node through devfs");
    compare_inode(fd,"/dev/input/event0",13,64);
    uint8_t bits[8]={0};
    require(ioctl(fd,EVIOCGBIT(0,sizeof(bits)),bits)>=0,
            "evdev real capability ioctl through VFS-mounted FileOps");
    require(close(fd)==0,"close evdev client");
    int master=posix_openpt(O_RDWR|O_NOCTTY);
    require(master>=0,"open ptmx through mounted devfs");
    compare_inode(master,"/dev/ptmx",5,2);
    require(grantpt(master)==0 && unlockpt(master)==0,
            "grant and unlock real PTY slave");
    const char *slave=ptsname(master);
    require(slave && !strncmp(slave,"/dev/pts/",9),
            "PTY slave path belongs to devfs pts directory");
    require(directory_has("/dev/pts",slave+9),
            "live PTY slave appears in devfs getdents");
    int peer=open(slave,O_RDWR|O_NOCTTY);
    require(peer>=0,"open dynamic PTY slave through VFS");
    struct stat st={0};
    require(fstat(peer,&st)==0 && S_ISCHR(st.st_mode),
            "slave fstat preserves devfs character type");
    require(close(peer)==0 && close(master)==0,"release PTY both ends");
    puts("M174_DEVFS_HARDWARE_PTY_PASS");fflush(stdout);
}
static void exercise_original_software(void) {
    /* The signed Alpine ELFs are executed as unmodified guest processes.
     * Separate help/entry checks from dbus-uuidgen and mpstat real semantics. */
    pid_t child=fork();
    require(child>=0,"fork original Alpine guest batch");
    if(child==0) {
        char *const args[]={"/bin/busybox","sh","/tests/m174-software-run.sh",NULL};
        execv(args[0],args);
        _exit(127);
    }
    int status=0;
    require(waitpid(child,&status,0)==child,"wait original Alpine batch");
    require(WIFEXITED(status) && WEXITSTATUS(status)==0,
            "20 original signed Alpine entrypoints and representative functions");
    puts("M174_DEVFS_SOFTWARE_INTEGRATION_PASS");fflush(stdout);
}
int main(void) {
    exercise_pseudo_devices();
    exercise_hardware_and_pty();
    puts("M174_DEVFS_VFS_PASS");fflush(stdout);
    exercise_original_software();
    char *const argv[]={"/usr/bin/naseed-desktop","m173",NULL};
    execv(argv[0],argv);
    require(0,"exec preserved independently launched M173 desktop");
    return 74;
}
