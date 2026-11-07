/* A deterministic REAL Alpine musl struct utmp record, target-native ELF.
 *
 * This fixture is not a logged-in NaSeed session and is never counted as
 * an original Alpine executable.  It provides independent binary bytes for
 * testing the signed original util-linux utmpdump reader and -r writer.
 *
 * We compile this target-native with GNU cross-GCC, whose <utmp.h> is glibc,
 * while the ORIGINAL Alpine utmpdump was built against MUSL <utmpx.h>.
 * Therefore deliberately mirror the exact musl 64-bit wire ABI instead of
 * accidentally emitting a glibc 384-byte x86 record.  Source of the ABI:
 * https://git.musl-libc.org/cgit/musl/tree/include/utmpx.h
 * (musl: int ut_session,__ut_pad2; struct timeval ut_tv).  Explicit offsets,
 * target pointer width and endian checks ensure cross-ISA byte parity.
 */
#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <utmp.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "M219 accepts only the three little-endian 64-bit Alpine targets"
#endif
#if !(defined(__x86_64__) || defined(__aarch64__) || (defined(__riscv) && __riscv_xlen == 64))
#error "M219 musl utmp ABI verified only for x86-64, AArch64 and RV64"
#endif

struct m219_musl_utmp {
    short ut_type;
    short __ut_pad1;
    int32_t ut_pid;
    char ut_line[32];
    char ut_id[4];
    char ut_user[32];
    char ut_host[256];
    struct { short __e_termination, __e_exit; } ut_exit;
    int32_t ut_session, __ut_pad2;
    struct timeval ut_tv;
    uint32_t ut_addr_v6[4];
    char __unused[20];
};

_Static_assert(sizeof(void *) == 8, "M219 requires 64-bit target");
_Static_assert(sizeof(struct timeval) == 16, "M219 timeval64 musl ABI");
_Static_assert(sizeof(struct m219_musl_utmp) == 400, "M219 musl utmp size");
_Static_assert(offsetof(struct m219_musl_utmp, ut_pid) == 4, "M219 pid offset");
_Static_assert(offsetof(struct m219_musl_utmp, ut_user) == 44, "M219 user offset");
_Static_assert(offsetof(struct m219_musl_utmp, ut_host) == 76, "M219 host offset");
_Static_assert(offsetof(struct m219_musl_utmp, ut_session) == 336, "M219 session offset");
_Static_assert(offsetof(struct m219_musl_utmp, ut_tv) == 344, "M219 timestamp offset");
_Static_assert(offsetof(struct m219_musl_utmp, ut_addr_v6) == 360, "M219 IPv4 offset");
_Static_assert(offsetof(struct m219_musl_utmp, __unused) == 376, "M219 tail offset");
_Static_assert(USER_PROCESS == 7, "M219 USER_PROCESS UAPI drift");

static void make_record(struct m219_musl_utmp *record, int edited) {
    memset(record, 0, sizeof(*record));
    record->ut_type = USER_PROCESS;
    record->ut_pid = 21942;
    memcpy(record->ut_id, "T219", 4);
    memcpy(record->ut_user, edited ? "changed" : "naseed", edited ? 7 : 6);
    memcpy(record->ut_line, "ttyS1", 5);
    memcpy(record->ut_host, "alpine.test", 11);
    record->ut_tv.tv_sec = 1700000000;
    record->ut_tv.tv_usec = 0;
    if (inet_pton(AF_INET, "192.0.2.55", record->ut_addr_v6) != 1) {
        fputs("M219 fixture: IPv4 encoding failed\n", stderr);
        exit(2);
    }
}

static int write_record(const char *path) {
    struct m219_musl_utmp expected;
    make_record(&expected, 0);
    FILE *output = fopen(path, "wb");
    if (!output) {
        fprintf(stderr, "M219 fixture: create %s: %s\n", path, strerror(errno));
        return 1;
    }
    int okay = fwrite(&expected, sizeof(expected), 1, output) == 1;
    if (fclose(output) != 0) okay = 0;
    if (!okay) {
        fprintf(stderr, "M219 fixture: incomplete %s write\n", path);
        return 1;
    }
    printf("M219_UTMP_RECORD_BYTES=%zu\n", sizeof(expected));
    return 0;
}

static int verify_record(const char *path, int edited) {
    struct m219_musl_utmp expected;
    make_record(&expected, edited);
    FILE *input = fopen(path, "rb");
    if (!input) {
        fprintf(stderr, "M219 fixture: read %s: %s\n", path, strerror(errno));
        return 1;
    }
    struct m219_musl_utmp actual;
    int okay = fread(&actual, sizeof(actual), 1, input) == 1 &&
               memcmp(&actual, &expected, sizeof(actual)) == 0 &&
               fgetc(input) == EOF && !ferror(input);
    if (fclose(input) != 0) okay = 0;
    if (!okay) {
        fprintf(stderr, "M219 fixture: record has wrong native ABI bytes, "
                        "fields, length or trailing data: %s\n", path);
        return 1;
    }
    printf("M219_UTMP_EXACT_%s_BYTES_PASS=%zu\n",
           edited ? "EDITED" : "ORIGINAL", sizeof(actual));
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("usage: m219-utmpdump-fixture --write|--verify-original|"
              "--verify-edited FILE\n", stderr);
        return 2;
    }
    if (strcmp(argv[1], "--write") == 0) return write_record(argv[2]);
    if (strcmp(argv[1], "--verify-original") == 0)
        return verify_record(argv[2], 0);
    if (strcmp(argv[1], "--verify-edited") == 0)
        return verify_record(argv[2], 1);
    fputs("M219 fixture: unsupported operation\n", stderr);
    return 2;
}
