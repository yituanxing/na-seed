/* Independent Linux syscall oracle for F_GETPIPE_SZ/F_SETPIPE_SZ behavior.
 * Dynamic growth may be restricted by host per-user pipe quotas. NaSeed's
 * own ring growth is tested separately without depending on that quota. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd[2];
    assert(pipe2(fd,O_NONBLOCK)==0);
    int initial=fcntl(fd[0],F_GETPIPE_SZ);
    assert(initial>=4096 && fcntl(fd[1],F_GETPIPE_SZ)==initial);
    assert(fcntl(fd[1],F_SETPIPE_SZ,0)==4096);
    assert(fcntl(fd[0],F_GETPIPE_SZ)==4096);
    assert(fcntl(fd[0],F_SETPIPE_SZ,-1)==-1 && errno==EINVAL);
    int other=open("/dev/null",O_RDONLY);
    assert(other>=0);
    assert(fcntl(other,F_GETPIPE_SZ)==-1 && errno==EBADF);
    assert(fcntl(other,F_SETPIPE_SZ,8192)==-1 && errno==EBADF);
    close(other);
    int increased=fcntl(fd[1],F_SETPIPE_SZ,5000);
    if (increased>=0) {
        assert(increased==8192 && fcntl(fd[0],F_GETPIPE_SZ)==8192);
        char bytes[5000]={0};
        assert(write(fd[1],bytes,sizeof(bytes))==(ssize_t)sizeof(bytes));
        assert(fcntl(fd[0],F_SETPIPE_SZ,4096)==-1 && errno==EBUSY);
        assert(read(fd[0],bytes,sizeof(bytes))==(ssize_t)sizeof(bytes));
        assert(fcntl(fd[0],F_SETPIPE_SZ,4096)==4096);
    } else {
        assert(errno==EPERM || errno==ENOMEM);
        puts("[LINUX PIPE ORACLE] host per-user quota disallows growth; kernel ring growth tested independently");
    }
    close(fd[0]);
    close(fd[1]);
    puts("[LINUX PIPE FCNTL PASS] default, shared endpoints, min/invalid capacity, FD kind, conditional grow/shrink");
    return 0;
}
