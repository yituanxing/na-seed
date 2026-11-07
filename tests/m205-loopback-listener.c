/* M205 fixture only: create a genuine kernel TCP LISTEN socket for Alpine ss.
 * Not an accepted cohort ELF or a substitute for the original signed ss.
 * Static linking avoids depending on the host glibc loader inside Alpine. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static void terminate(int signo) { (void)signo; stop=1; }

int main(void) {
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) { perror("M205_LISTENER_SOCKET"); return 1; }
    int reuse=1;
    if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))<0) {
        perror("M205_LISTENER_REUSEADDR"); close(fd); return 1;
    }
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(18085);
    if(inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr)!=1 ||
       bind(fd,(const struct sockaddr *)&addr,sizeof(addr))<0 ||
       listen(fd,4)<0) {
        perror("M205_LISTENER_BIND_LISTEN"); close(fd); return 1;
    }
    if(signal(SIGTERM,terminate)==SIG_ERR ||
       signal(SIGINT,terminate)==SIG_ERR) {
        perror("M205_LISTENER_SIGNAL"); close(fd); return 1;
    }
    puts("M205_LOOPBACK_LISTEN_READY");
    fflush(stdout);
    while(!stop) sleep(1);
    close(fd);
    return 0;
}
