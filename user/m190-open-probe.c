/* M190 diagnostic only: which exact Linux open() flags can still write a
 * live ext4 inode? No modification of the original editor document.
 * Source compiled for each target ISA on the host; signed Alpine apps stay
 * pristine and remain the genuine functional acceptance.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static void diagnostic(const char *name,const char *path,int flags) {
    errno=0;
    int fd=open(path,flags,0644);
    int err=fd<0?errno:0;
    printf("M190 %s fd=%d errno=%d\n",name,fd,err);
    if(fd>=0) {
        struct stat st={0};
        if(fstat(fd,&st)==0)
            printf("M190 inode mode=%o uid=%u size=%lld\n",
                   (unsigned)st.st_mode,(unsigned)st.st_uid,
                   (long long)st.st_size);
        else printf("M190 fstat errno=%d\n",errno);
        if(close(fd)<0)printf("M190 close errno=%d\n",errno);
    }
}
int main(void) {
    diagnostic("read-old","/async/m190-editor.txt",O_RDONLY);
    diagnostic("write-old","/async/m190-editor.txt",O_WRONLY);
    diagnostic("newfile","/async/m190-open-probe.txt",
               O_CREAT|O_EXCL|O_WRONLY);
    diagnostic("trunc-old","/async/m190-open-probe.txt",O_TRUNC|O_WRONLY);
    fflush(stdout);
    return 0;
}
