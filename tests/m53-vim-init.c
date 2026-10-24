#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
/* Standard unmodified Linux userspace program: PID1 owns a real UART tty,
 * forks and execs original packaged Vim. After physical serial keystrokes,
 * verify the actual saved tmpfs bytes. No synthetic /dev/tty or kernel hook. */
static int fail(const char *where) {
    const char prefix[]="[M53 FAIL] ";
    (void)write(STDERR_FILENO,prefix,sizeof(prefix)-1);
    (void)write(STDERR_FILENO,where,strlen(where));
    (void)write(STDERR_FILENO,"\n",1);
    return 90;
}
int main(void) {
    if(ioctl(0,TIOCSCTTY,0)!=0)return fail("TIOCSCTTY");
    int tty=open("/dev/tty",O_RDWR);
    if(tty<0)return fail("open real TTY");
    for(int n=0;n<3;n++)
        if(dup2(tty,n)!=n)return fail("dup tty");
    if(tty>2)close(tty);
    struct termios t;
    if(tcgetattr(0,&t)!=0)return fail("tcgetattr");
    t.c_lflag|=ICANON|ECHO;
    t.c_iflag|=ICRNL;
    if(tcsetattr(0,TCSANOW,&t)!=0)return fail("tcsetattr");
    const char *file="/tmp/m53-live-vim.txt";
    int fd=open(file,O_CREAT|O_TRUNC|O_WRONLY,0644);
    if(fd<0)return fail("create real tmpfs note");
    const char source[]="M53 original Vim serial session\n";
    if(write(fd,source,sizeof(source)-1)!=(ssize_t)(sizeof(source)-1))
        return fail("write original file");
    if(close(fd)!=0)return fail("close real file");
    const char banner[]="[M53] launching original native Vim over actual controlling UART\n";
    (void)write(STDOUT_FILENO,banner,sizeof(banner)-1);
    pid_t child=fork();
    if(child<0)return fail("fork");
    if(child==0) {
        char *const argv[]={"/usr/bin/vim","-Nu","NONE","-i","NONE",
                            "-n","-N",(char*)file,NULL};
        char *const envp[]={"TERM=vt100","HOME=/tmp","PATH=/usr/bin:/bin",
                            "LC_ALL=C","LANG=C",NULL};
        execve(argv[0],argv,envp);
        return fail("exec original Alpine Vim ELF");
    }
    int status=0;
    if(waitpid(child,&status,0)!=child)return fail("wait genuine Vim child");
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)
        return fail("Vim did not exit successfully");
    fd=open(file,O_RDONLY);
    if(fd<0)return fail("reopen saved Vim file");
    char data[128]={0};
    ssize_t count=read(fd,data,sizeof(data)-1);
    close(fd);
    if(count<0 || strstr(data,"M53-LIVE-EDIT M53 original Vim serial session")==NULL)
        return fail("real serial Vim editor did not save expected tmpfs bytes");
    const char passed[]="M53_ORIGINAL_VIM_REAL_TTY_EDIT_SAVE_PASS\n";
    (void)write(STDOUT_FILENO,passed,sizeof(passed)-1);
    return 0;
}
