/* Original Alpine GCC/musl builds this inside NaSeed; no custom ABI. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
int main(void) {
    int fd=open("/tests/readv.txt",O_RDONLY);
    if (fd<0) return 11;
    char a[5]={0}, b[8]={0}, c[4]={0};
    struct iovec iov[3]={{a,4},{b,7},{c,3}};
    ssize_t n=readv(fd,iov,3);
    if (n!=14 || memcmp(a,"abcd",4)!=0 ||
        memcmp(b,"efghijk",7)!=0 || memcmp(c,"lmn",3)!=0) return 12;
    if (readv(fd,iov,3)!=0) return 13;
    if (close(fd)) return 14;
    puts("M38_READV_PASS");
    return 0;
}
