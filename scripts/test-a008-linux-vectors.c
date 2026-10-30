/* NAS-A008: genuine same-ISA Linux oracle for vectored descriptor semantics.
 * This program uses libc/Linux syscalls, never NaSeed mocks. Keep the cases
 * mirrored in test-m106-usercopy-syscalls.c so an apparent NaSeed fix cannot
 * accidentally redefine the expected behavior.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

static void expect_failure(ssize_t result, int error) {
    assert(result == -1);
    assert(errno == error);
}

static void eventfd_vectors(void) {
    int fd=eventfd(7,EFD_NONBLOCK);
    assert(fd>=0);
    uint32_t halves[2]={0,0};
    struct iovec split[2]={{&halves[0],4},{&halves[1],4}};
    assert(readv(fd,split,2)==8);
    uint64_t value=0;
    memcpy(&value,halves,sizeof(value));
    assert(value==7);
    errno=0;
    expect_failure(read(fd,&value,8),EAGAIN);
    assert(close(fd)==0);

    uint64_t seven=7,eleven=11;
    struct iovec leading_zero[2]={{&seven,0},{&seven,8}};
    fd=eventfd(0,EFD_NONBLOCK);
    assert(fd>=0);
    errno=0;
    expect_failure(writev(fd,leading_zero,2),EINVAL);
    assert(eventfd_read(fd,&value)==-1 && errno==EAGAIN);
    struct iovec trailing_zero[2]={{&seven,8},{&seven,0}};
    assert(writev(fd,trailing_zero,2)==8);
    assert(eventfd_read(fd,&value)==0 && value==7);
    struct iovec two_words[2]={{&seven,8},{&eleven,8}};
    assert(writev(fd,two_words,2)==16);
    assert(eventfd_read(fd,&value)==0 && value==18);
    struct iovec two_halves[2]={{&seven,4},{(uint8_t *)&seven+4,4}};
    errno=0;
    expect_failure(writev(fd,two_halves,2),EINVAL);
    assert(close(fd)==0);
    puts("[NAS-A008 LINUX EVENTFD PASS] readv 4+4; writev 0+8 EINVAL, 8+0, 8+8, 4+4 EINVAL");
}

static void pipe_atomic_vectors(void) {
    int fds[2];
    assert(pipe2(fds,O_NONBLOCK)==0);
    /* PIPE_BUF and NaSeed's pipe capacity are 4096. Lower Linux's pipe
     * capacity so the same 4000/96-byte admission is directly comparable. */
    assert(fcntl(fds[1],F_SETPIPE_SZ,4096)==4096);
    char baseline[4000],first[50],second[100],drained[4096];
    memset(baseline,'X',sizeof(baseline));
    memset(first,'A',sizeof(first));
    memset(second,'B',sizeof(second));
    assert(write(fds[1],baseline,sizeof(baseline))==(ssize_t)sizeof(baseline));
    struct iovec too_big[2]={{first,50},{second,100}};
    errno=0;
    expect_failure(writev(fds[1],too_big,2),EAGAIN);
    /* An aggregate write fitting the *same* 96 bytes must succeed. */
    struct iovec fits[2]={{first,50},{second,40}};
    assert(writev(fds[1],fits,2)==90);
    assert(read(fds[0],drained,4090)==4090);
    assert(memcmp(drained,baseline,4000)==0);
    assert(memcmp(drained+4000,first,50)==0);
    assert(memcmp(drained+4050,second,40)==0);
    errno=0;
    expect_failure(read(fds[0],drained,1),EAGAIN);

    struct iovec malformed[2]={{first,2},{(void *)(uintptr_t)1,2}};
    errno=0;
    expect_failure(writev(fds[1],malformed,2),EFAULT);
    errno=0;
    expect_failure(read(fds[0],drained,1),EAGAIN);
    /* A second iovec descriptor on an inaccessible metadata page must
     * also fail BEFORE the good first segment modifies the pipe. */
    long page_size=sysconf(_SC_PAGESIZE);
    assert(page_size>=4096);
    uint8_t *mapping=mmap(NULL,(size_t)page_size*2,PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(mapping!=MAP_FAILED);
    struct iovec *edge=(struct iovec *)(mapping+page_size-sizeof(*edge));
    edge[0]=(struct iovec){first,2};
    assert(mprotect(mapping+page_size,(size_t)page_size,PROT_NONE)==0);
    errno=0;
    expect_failure(writev(fds[1],edge,2),EFAULT);
    errno=0;
    expect_failure(read(fds[0],drained,1),EAGAIN);
    assert(munmap(mapping,(size_t)page_size*2)==0);
    assert(close(fds[0])==0 && close(fds[1])==0);
    puts("[NAS-A008 LINUX PIPE PASS] nonblock aggregate admission, later-invalid source and metadata rollback");
}

static void vector_validation(void) {
    int fds[2];
    assert(pipe2(fds,O_NONBLOCK)==0);
    struct iovec many[1025];
    memset(many,0,sizeof(many));
    assert(readv(fds[0],many,0)==0);
    assert(writev(fds[1],many,0)==0);
    assert(readv(fds[0],many,1024)==0);
    assert(writev(fds[1],many,1024)==0);
    errno=0;
    expect_failure(readv(fds[0],many,1025),EINVAL);
    errno=0;
    expect_failure(writev(fds[1],many,1025),EINVAL);
    errno=0;
    expect_failure(readv(-1,many,0),EBADF);
    errno=0;
    expect_failure(writev(-1,many,0),EBADF);
    errno=0;
    expect_failure(readv(fds[1],many,0),EBADF);
    errno=0;
    expect_failure(writev(fds[0],many,0),EBADF);
    errno=0;
    expect_failure(readv(-1,many,1025),EBADF);
    errno=0;
    expect_failure(writev(-1,many,1025),EBADF);
    struct iovec *invalid=(struct iovec *)(uintptr_t)1;
    /* Raw syscalls avoid glibc's compile-time access attribute on iovec,
     * which correctly warns that the deliberately invalid pointer cannot
     * denote a C object. The test is about Linux's error precedence. */
    errno=0;
    expect_failure(syscall(SYS_readv,-1,invalid,1),EBADF);
    errno=0;
    expect_failure(syscall(SYS_writev,fds[0],invalid,1),EBADF);
    errno=0;
    expect_failure(syscall(SYS_readv,fds[0],invalid,1),EFAULT);
    errno=0;
    expect_failure(syscall(SYS_writev,fds[1],invalid,1),EFAULT);
    assert(close(fds[0])==0 && close(fds[1])==0);
    puts("[NAS-A008 LINUX IOV PASS] empty FD checks, 1024/1025, EBADF-before-usercopy");
}

static void vector_length_extremes(void) {
    /* /dev/null has no user-data transfer: exercise Linux iov_iter import
     * limits without allocating/copying multi-gigabyte buffers. Linux still
     * iterates about 2 GiB for successful writev, so keep that slow evidence
     * explicit-only; automatic CI checks the fast rejection paths. */
    assert(sizeof(size_t)==8);
    const size_t max_rw=(size_t)0x7ffff000ULL;
    int rd=open("/dev/null",O_RDONLY),wr=open("/dev/null",O_WRONLY);
    assert(rd>=0 && wr>=0);
    struct iovec v[2]={{NULL,(size_t)INT64_MAX},{NULL,0}};
    assert(readv(rd,v,1)==0);
    v[0].iov_len=(size_t)UINT64_C(1)<<63;
    errno=0;
    expect_failure(readv(rd,v,1),EINVAL);
    errno=0;
    expect_failure(writev(wr,v,1),EINVAL);
    v[0]=(struct iovec){NULL,max_rw};
    v[1]=(struct iovec){NULL,(size_t)INT64_MAX};
    errno=0;
    expect_failure(writev(wr,v,2),EFAULT);
    v[0]=(struct iovec){(void *)(uintptr_t)1,8};
    v[1]=(struct iovec){NULL,(size_t)UINT64_C(1)<<63};
    errno=0;
    expect_failure(writev(wr,v,2),EINVAL);
    puts("[NAS-A008 LINUX MAX_RW FAST PASS] signed lengths and full multi-iov validation");
    const char *deep=getenv("NAS_A008_DEEP_MAX_RW");
    if(deep && strcmp(deep,"1")==0){
        v[0]=(struct iovec){NULL,(size_t)INT64_MAX};
        assert(writev(wr,v,1)==(ssize_t)max_rw);
        v[0]=(struct iovec){NULL,max_rw};
        v[1]=(struct iovec){NULL,8};
        assert(writev(wr,v,2)==(ssize_t)max_rw);
        puts("[NAS-A008 LINUX MAX_RW DEEP PASS] single-iov and multi-iov 2-GiB virtual-clamp parity");
    }
    assert(close(rd)==0 && close(wr)==0);
}

int main(void) {
    eventfd_vectors();
    pipe_atomic_vectors();
    vector_validation();
    vector_length_extremes();
    puts("[NAS-A008 LINUX ORACLE PASS]");
    return 0;
}
