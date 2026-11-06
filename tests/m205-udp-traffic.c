/* Real IPv4 UDP/DNS wire stimulus for upstream iftop's AF_PACKET capture.
 * Unlike BusyBox ping, SOCK_DGRAM needs no unsupported AF_INET/SOCK_RAW.
 * The guest's existing lwIP route carries every real packet through eth0.
 * This is NOT the program under test: the original signed Alpine iftop is. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { PACKET_COUNT = 40 };

int main(void) {
    const unsigned char query[] = {
        0x20, 0x50, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01
    };
    const struct timespec pace = {.tv_sec = 0, .tv_nsec = 250000000L};
    struct sockaddr_in dns = {.sin_family = AF_INET, .sin_port = htons(53)};
    if (inet_pton(AF_INET, "10.0.2.3", &dns.sin_addr) != 1)
        return 2;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "M205 UDP socket: %s\n", strerror(errno));
        return 1;
    }
    sleep(1); /* let the unchanged original iftop open its capture socket */
    for (unsigned i = 0; i < PACKET_COUNT; ++i) {
        unsigned char packet[sizeof(query)];
        memcpy(packet, query, sizeof(packet));
        packet[0] = (unsigned char)(i >> 8);
        packet[1] = (unsigned char)i;
        if (sendto(fd, packet, sizeof(packet), 0,
                   (const struct sockaddr *)&dns, sizeof(dns)) !=
            (ssize_t)sizeof(packet)) {
            fprintf(stderr, "M205 UDP send %u: %s\n", i, strerror(errno));
            close(fd);
            return 1;
        }
        struct timespec remaining = pace;
        while (nanosleep(&remaining, &remaining) != 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "M205 UDP pace: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
    }
    close(fd);
    printf("M205_REAL_UDP_SENT=%u dst=10.0.2.3:53\n", PACKET_COUNT);
    return 0;
}
