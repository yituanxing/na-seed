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
#include <sys/uio.h>
#include <string.h>
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
    /* A real Alpine Nano stdio flush calls writev(), not scalar write.
     * Use a separate scratch inode so this probe cannot manufacture Nano's
     * saved document. Independent readv verifies genuine IRQ-backed data. */
    static const char first[]="M190_VECTOR_";
    static const char second[]="WORKER_IRQ\n";
    static const char *scratch="/async/m190-open-probe.txt";
    int fd=open(scratch,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0) {
        printf("[M190 IOV FAIL] scratch open errno=%d\n",errno);
        fflush(stdout);
        return 190;
    }
    struct iovec source[3]={
        {.iov_base=(void *)first,.iov_len=sizeof(first)-1},
        {.iov_base=(void *)second,.iov_len=sizeof(second)-1},
        {.iov_base=NULL,.iov_len=0},
    };
    const size_t expected=sizeof(first)+sizeof(second)-2;
    errno=0;
    ssize_t wrote=writev(fd,source,3);
    int write_errno=errno;
    printf("[M190 IOV WRITE] result=%lld expected=%zu errno=%d\n",
           (long long)wrote,expected,write_errno);
    if(wrote!=(ssize_t)expected || fsync(fd)!=0 || close(fd)!=0) {
        puts("[M190 IOV FAIL] vector write/fsync/close");
        fflush(stdout);
        return 190;
    }
    fd=open(scratch,O_RDONLY);
    if(fd<0)return 190;
    char out_first[sizeof(first)]={0};
    char out_second[sizeof(second)]={0};
    struct iovec dest[3]={
        {.iov_base=out_first,.iov_len=sizeof(first)-1},
        {.iov_base=out_second,.iov_len=sizeof(second)-1},
        {.iov_base=NULL,.iov_len=0},
    };
    errno=0;
    ssize_t got=readv(fd,dest,3);
    int read_errno=errno;
    printf("[M190 IOV READ] result=%lld expected=%zu errno=%d\n",
           (long long)got,expected,read_errno);
    if(got!=(ssize_t)expected ||
       memcmp(out_first,first,sizeof(first)-1) ||
       memcmp(out_second,second,sizeof(second)-1) ||
       close(fd)!=0) {
        puts("[M190 IOV FAIL] vector readback differs");
        fflush(stdout);
        return 190;
    }
    puts("M190_VECTORED_IRQ_FS_FUNCTION_PASS");
    fflush(stdout);
    return 0;
}
