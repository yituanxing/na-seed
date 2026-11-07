/* Independent host-Linux oracle: a writable MAP_SHARED page includes bytes
 * beyond the file's partial EOF, but those bytes are not file contents when
 * a later pwrite/ftruncate exposes the old hole. The NaSeed tmpfs producer
 * fixture exercises the same contract on its actual sparse-frame backend. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
    char path[]="/tmp/naseed-a005-mmap-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    assert(unlink(path)==0);
    char first[100];
    memset(first,'a',sizeof(first));
    assert(write(fd,first,sizeof(first))==(ssize_t)sizeof(first));
    uint8_t *mapped=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    assert(mapped!=MAP_FAILED);
    mapped[200]='X'; /* mapped, but beyond current i_size=100 */
    char z='Z',seen=1;
    assert(pwrite(fd,&z,1,300)==1);
    assert(pread(fd,&seen,1,200)==1 && seen==0);
    mapped[350]='Y'; /* again beyond the new i_size=301 */
    assert(ftruncate(fd,400)==0);
    seen=1;
    assert(pread(fd,&seen,1,350)==1 && seen==0);
    assert(msync(mapped,4096,MS_ASYNC)==0);
    assert(munmap(mapped,4096)==0);
    assert(close(fd)==0);
    puts("[A005 LINUX MMAP EOF HOLE PASS] pwrite and ftruncate reveal zeros, not stale mapped tail");
    return 0;
}
