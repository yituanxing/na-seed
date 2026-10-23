/* Host regression for genuinely large CPIO archives, not ISA-specific code.
 * Keep real archive parser/lookup implementation, mock only frame provider. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bootfs.h"

#define ARCHIVE_CAP (512U * 1024U)
static unsigned char image[ARCHIVE_CAP];
static size_t cursor;
static unsigned inode;

uint64_t nas_hhdm_offset(void) { return 0; }
uint64_t nas_frame_alloc(void) {
    void *p = aligned_alloc(4096, 4096);
    if (!p) return 0;
    memset(p, 0, 4096);
    return (uint64_t)(uintptr_t)p;
}
bool nas_frame_free(uint64_t address) {
    free((void *)(uintptr_t)address);
    return true;
}
static void fail(const char *msg) {
    fprintf(stderr, "[BOOTFS SCALE FAIL] %s\n", msg);
    exit(1);
}
static void append(const char *name, unsigned mode, const char *payload) {
    const unsigned len = (unsigned)strlen(name) + 1U;
    const unsigned data_len = payload ? (unsigned)strlen(payload) : 0U;
    char header[111];
    int n = snprintf(header, sizeof(header),
        "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        ++inode, mode, 0U, 0U, 1U, 0U, data_len,
        0U, 0U, 0U, 0U, len, 0U);
    if (n != 110 || cursor + 110 + len + data_len + 16 > sizeof(image))
        fail("malformed test archive");
    memcpy(image + cursor, header, 110);
    cursor += 110;
    memcpy(image + cursor, name, len);
    cursor += len;
    cursor = (cursor + 3U) & ~3U;
    if (data_len) memcpy(image + cursor, payload, data_len);
    cursor += data_len;
    cursor = (cursor + 3U) & ~3U;
}
int main(void) {
    append(".", 0040755, 0);
    append("tests", 0040755, 0);
    char path[64];
    for (unsigned i = 0; i < 1200; ++i) {
        snprintf(path, sizeof(path), "tests/file-%04u", i);
        append(path, 0100644, "real-bytes");
    }
    append("tests/alias", 0120777, "file-0017");
    append("TRAILER!!!", 0, 0);
    if (!bootfs_mount(image, cursor) || !bootfs_mounted())
        fail("large archive mount");
    const struct bootfs_entry *dir = bootfs_find("/tests");
    if (!dir || !bootfs_is_dir(dir)) fail("directory lookup");
    for (unsigned i = 0; i < 1200; ++i) {
        snprintf(path, sizeof(path), "/tests/file-%04u", i);
        const struct bootfs_entry *entry = bootfs_find(path);
        if (!entry || entry->size != 10 || memcmp(entry->data,"real-bytes",10))
            fail("hashed payload lookup");
        if (bootfs_child(dir, i) != entry) fail("ordered direct children");
    }
    const struct bootfs_entry *alias = bootfs_child(dir, 1200);
    if (!alias || !bootfs_is_link(alias) || bootfs_child(dir, 1201))
        fail("directory boundary/symlink");
    char resolved[256];
    if (bootfs_resolve("/tests/alias", true, resolved) ||
        strcmp(resolved, "/tests/file-0017"))
        fail("relative symlink semantics");
    /* Remount reclaims every slab. Same path twice still fails closed. */
    cursor = 0; inode = 0;
    append(".", 0040755, 0);
    append("tests", 0040755, 0);
    append("tests/file", 0100644, "A");
    append("tests/file", 0100644, "B");
    append("TRAILER!!!", 0, 0);
    if (bootfs_mount(image, cursor) || bootfs_mounted())
        fail("duplicate path rejection");
    puts("[BOOTFS SCALE PASS] 1200 files, slabs, hash, readdir, links, remount, duplicate rejection");
    return 0;
}
