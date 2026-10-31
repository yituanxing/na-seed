#include "tmpfs.h"
#include "task.h"
#include "fs/vfs.h"
#include "bootfs.h"
#include "errno.h"
#include "lib/string.h"
#include "mm/frame.h"
#include "time/clock.h"
#include "arch/arch.h"

/* Metadata is demand-allocated from the common frame provider. One page
 * yields stable inode slots, recycled through an O(1) free-list after the
 * FINAL VFS pin has gone away; no scan of all past allocations is needed. */
#define TMP_INODES_PER_SLAB (4096U / sizeof(struct nas_tmp_file))
struct tmp_inode_slab {
    struct nas_tmp_file nodes[TMP_INODES_PER_SLAB];
};
_Static_assert(sizeof(struct tmp_inode_slab) <= 4096, "tmp inode slab must fit one frame");
_Static_assert(TMP_INODES_PER_SLAB >= 2, "tmp inode must fit slab");

/* Use the existing cross-ISA RTC-anchored timekeeper; if firmware has
 * no valid RTC, monotonic nanoseconds still order writes within this boot.
 * Never use synthetic always-equal timestamps for real GNU Make. */
static uint64_t tmp_now_ns(void) {
    uint64_t now = 0;
    return nas_time_realtime_ns(&now) ? now : nas_arch_monotonic_ns();
}
static uint64_t tmp_next_mtime(uint64_t previous) {
    uint64_t now = tmp_now_ns();
    return now > previous ? now : previous + (previous < UINT64_MAX);
}
/* /tmp is a writable overlay whose root inode ID is borrowed from bootfs.
 * Its namespace timestamps must never mutate the immutable CPIO entry. */
static uint64_t tmp_root_mtime_ns, tmp_root_ctime_ns;
void tmp_root_times(uint64_t *mtime_ns,uint64_t *ctime_ns) {
    if(!tmp_root_mtime_ns) {
        uint64_t now=tmp_now_ns();
        if(!now)now=1;
        tmp_root_mtime_ns=tmp_root_ctime_ns=now;
    }
    if(mtime_ns)*mtime_ns=tmp_root_mtime_ns;
    if(ctime_ns)*ctime_ns=tmp_root_ctime_ns;
}
void tmp_root_dir_modified(void) {
    tmp_root_times(0,0);
    uint64_t before=tmp_root_mtime_ns>tmp_root_ctime_ns ?
                    tmp_root_mtime_ns : tmp_root_ctime_ns;
    tmp_root_mtime_ns=tmp_root_ctime_ns=tmp_next_mtime(before);
}
void tmp_dir_modified(struct nas_tmp_file *directory) {
    if(!directory || !directory->used || !directory->is_dir)return;
    uint64_t before=directory->mtime_ns>directory->ctime_ns ?
                    directory->mtime_ns : directory->ctime_ns;
    directory->mtime_ns=directory->ctime_ns=tmp_next_mtime(before);
}
static struct nas_tmp_file *tmp_free_inodes;
static uint64_t tmp_next_ino = 1;

static struct tmp_inode_slab *tmp_new_slab(void) {
    uint64_t frame = nas_frame_alloc();
    if (!frame) return 0;
    struct tmp_inode_slab *slab =
        (struct tmp_inode_slab *)(uintptr_t)(nas_hhdm_offset() + frame);
    memset(slab,0,4096);
    /* Seed a reusable slot list once per new page. Inode allocation no
     * longer rescans every live inode after thousands of creates. */
    for(unsigned i=TMP_INODES_PER_SLAB;i>0;i--) {
        struct nas_tmp_file *node=&slab->nodes[i-1];
        node->free_next=tmp_free_inodes;
        tmp_free_inodes=node;
    }
    return slab;
}
static void tmp_try_reclaim(struct nas_tmp_file *node) {
    if (!node || !node->used || node->nlink) return;
    /* Sole lifetime owner: mounted VFS inode pins cover shared Files,
     * independent opens, cwd, rename and orphan reclamation. */
    if(nas_vfs_inode_is_pinned(node))return;
    /* A detached directory has neither a visible alias nor children.
     * Refuse to recycle any unexpectedly live namespace index pointer. */
    if(node->directory_alias || node->children.count)return;
    tmp_truncate(node);
    if(node->xattr_frame) {
        (void)nas_frame_put(node->xattr_frame);
        node->xattr_frame=0;
    }
    node->used = false;
    node->free_next=tmp_free_inodes;
    tmp_free_inodes=node;
}


/* An optional root frame indexes 512 indirect pages; each indirect page
 * indexes 512 independently allocated 4-KiB file data pages. The common
 * frame allocator and HHDM supply all storage; no ISA-specific fast path. */
static uint64_t tmp_data_frame(struct nas_tmp_file *file,uint64_t number,
                               bool create) {
    if (!file || number >= TMP_INDEX_ROOT_ENTRIES * (uint64_t)TMP_PAGE_ENTRIES)
        return 0;
    if (!file->index_root) {
        if (!create) return 0;
        uint64_t root_addr = nas_frame_alloc();
        if (!root_addr) return 0;
        memset((void *)(uintptr_t)(nas_hhdm_offset() + root_addr),
               0, (size_t)TMP_PAGE_SIZE);
        file->index_root = root_addr;
    }
    uint64_t *root = (uint64_t *)(uintptr_t)(
        nas_hhdm_offset() + file->index_root);
    unsigned outer = (unsigned)(number / TMP_PAGE_ENTRIES);
    unsigned inner = (unsigned)(number % TMP_PAGE_ENTRIES);
    uint64_t table_addr = root[outer];
    if (!table_addr && create) {
        table_addr = nas_frame_alloc();
        if (!table_addr) return 0;
        memset((void *)(uintptr_t)(nas_hhdm_offset() + table_addr),
               0, (size_t)TMP_PAGE_SIZE);
        root[outer] = table_addr;
    }
    if (!table_addr) return 0;
    uint64_t *table = (uint64_t *)(uintptr_t)(nas_hhdm_offset() + table_addr);
    uint64_t data_addr = table[inner];
    if (!data_addr && create) {
        data_addr = nas_frame_alloc();
        if (!data_addr) return 0;
        memset((void *)(uintptr_t)(nas_hhdm_offset() + data_addr),
               0, (size_t)TMP_PAGE_SIZE);
        table[inner] = data_addr;
        if (number + 1 > file->highest_allocated_data_page_plus_one)
            file->highest_allocated_data_page_plus_one = number + 1;
    }
    return data_addr;
}
/* Linux xattr values can be 64 KiB; this first memory-backed provider has
 * a documented 4-KiB per-inode capacity, not a fabricated global table.
 * Packed entries avoid arbitrary per-name slot ceilings. Overflow returns
 * real ENOSPC/E2BIG. Metadata survives hard links, rename and open-unlink. */
struct tmp_xattr_page {
    uint16_t bytes,count;
    uint8_t data[4096-4];
};
_Static_assert(sizeof(struct tmp_xattr_page)==4096,"xattr page ABI");
static struct tmp_xattr_page *tmp_xattr(const struct nas_tmp_file *file) {
    return file && file->xattr_frame ?
        (struct tmp_xattr_page *)(uintptr_t)(nas_hhdm_offset()+file->xattr_frame) : NULL;
}
static int tmp_xattr_name(const char *name) {
    if(!name || !name[0])return -EINVAL;
    size_t n=strnlen(name,256);
    if(n==256)return -ERANGE;
    if(strncmp(name,"user.",5))return -EOPNOTSUPP;
    return n>5 ? 0 : -EINVAL;
}
/* An entry is [uint16 name length with NUL][uint16 value length][name][value].
 * Header fields are copied rather than dereferenced: entries may be unaligned. */
static bool tmp_xattr_entry(const struct tmp_xattr_page *page,unsigned at,
                            uint16_t *name_size,uint16_t *value_size) {
    if(!page || at>page->bytes || page->bytes-at<4)return false;
    memcpy(name_size,page->data+at,2);
    memcpy(value_size,page->data+at+2,2);
    return *name_size && *name_size<=256 &&
           (uint32_t)*name_size+*value_size<=page->bytes-at-4;
}
static int tmp_xattr_find(const struct tmp_xattr_page *page,
                          const char *name,unsigned *at_out,
                          uint16_t *name_len,uint16_t *value_len) {
    if(!page)return -ENODATA;
    unsigned at=0;
    while(at<page->bytes) {
        uint16_t n=0,v=0;
        if(!tmp_xattr_entry(page,at,&n,&v))return -EIO;
        if(!strcmp((const char *)page->data+at+4,name)) {
            if(at_out)*at_out=at;
            if(name_len)*name_len=n;
            if(value_len)*value_len=v;
            return 0;
        }
        at+=4U+n+v;
    }
    return -ENODATA;
}
int tmp_setxattr(struct nas_tmp_file *file,const char *name,
                 const uint8_t *value,uint64_t size,unsigned flags) {
    if(!file || !file->used)return -ENOENT;
    int valid=tmp_xattr_name(name);
    if(valid)return valid;
    if((flags&~3U) || flags==3)return -EINVAL;
    unsigned name_size=(unsigned)strlen(name)+1;
    if(size>sizeof(((struct tmp_xattr_page *)0)->data)-4-name_size)
        return -E2BIG;
    if(size && !value)return -EINVAL;
    struct tmp_xattr_page *page=tmp_xattr(file);
    unsigned at=0;
    uint16_t oldname=0,oldvalue=0;
    int found=tmp_xattr_find(page,name,&at,&oldname,&oldvalue);
    if(found && found!=-ENODATA)return found;
    if(!found && (flags&1U))return -EEXIST;
    if(found==-ENODATA && (flags&2U))return -ENODATA;
    unsigned oldbytes=!found ? 4U+oldname+oldvalue : 0;
    unsigned occupied=page ? page->bytes : 0;
    unsigned next=occupied-oldbytes+4U+name_size+(unsigned)size;
    if(next>sizeof(page->data))return -ENOSPC;
    if(!page) {
        uint64_t frame=nas_frame_alloc();
        if(!frame)return -ENOMEM;
        file->xattr_frame=frame;
        page=tmp_xattr(file);
        memset(page,0,4096);
    }
    if(oldbytes) {
        memmove(page->data+at,page->data+at+oldbytes,
                page->bytes-at-oldbytes);
        page->bytes-=(uint16_t)oldbytes;
        page->count--;
    }
    uint16_t header[2]={(uint16_t)name_size,(uint16_t)size};
    memcpy(page->data+page->bytes,header,4);
    memcpy(page->data+page->bytes+4,name,name_size);
    if(size)memcpy(page->data+page->bytes+4+name_size,value,(size_t)size);
    page->bytes=(uint16_t)next;
    page->count++;
    tmp_inode_touch(file);
    return 0;
}
int64_t tmp_getxattr(const struct nas_tmp_file *file,const char *name,
                     uint8_t *value,uint64_t capacity) {
    if(!file || !file->used)return -ENOENT;
    int valid=tmp_xattr_name(name);
    if(valid)return valid;
    struct tmp_xattr_page *page=tmp_xattr(file);
    unsigned at=0;
    uint16_t n=0,v=0;
    int rc=tmp_xattr_find(page,name,&at,&n,&v);
    if(rc)return rc;
    if(!value)return v;
    if(capacity<v)return -ERANGE;
    if(v)memcpy(value,page->data+at+4+n,v);
    return v;
}
int64_t tmp_listxattr(const struct nas_tmp_file *file,
                      uint8_t *names,uint64_t capacity) {
    if(!file || !file->used)return -ENOENT;
    struct tmp_xattr_page *page=tmp_xattr(file);
    if(!page)return 0;
    unsigned length=0;
    for(unsigned at=0;at<page->bytes;) {
        uint16_t n=0,v=0;
        if(!tmp_xattr_entry(page,at,&n,&v))return -EIO;
        if(names) {
            if(length+n>capacity)return -ERANGE;
            memcpy(names+length,page->data+at+4,n);
        }
        length+=n;
        at+=4U+n+v;
    }
    return length;
}
int tmp_removexattr(struct nas_tmp_file *file,const char *name) {
    if(!file || !file->used)return -ENOENT;
    int valid=tmp_xattr_name(name);
    if(valid)return valid;
    struct tmp_xattr_page *page=tmp_xattr(file);
    unsigned at=0;
    uint16_t n=0,v=0;
    int rc=tmp_xattr_find(page,name,&at,&n,&v);
    if(rc)return rc;
    unsigned occupied=4U+n+v;
    memmove(page->data+at,page->data+at+occupied,page->bytes-at-occupied);
    page->bytes-=(uint16_t)occupied;
    page->count--;
    if(!page->bytes) {
        (void)nas_frame_put(file->xattr_frame);
        file->xattr_frame=0;
    }
    tmp_inode_touch(file);
    return 0;
}
int tmp_fiemap_next(const struct nas_tmp_file *file,uint64_t start,
                    uint64_t end,uint64_t *logical,uint64_t *length,
                    bool *last) {
    if(!file || !file->used)return -ENOENT;
    if(file->is_dir)return -EISDIR;
    if(!logical || !length || !last)return -EINVAL;
    if(end>file->size)end=file->size;
    if(start>=end)return 0;
    uint64_t page=start/TMP_PAGE_SIZE;
    uint64_t final=(end+TMP_PAGE_SIZE-1)/TMP_PAGE_SIZE;
    while(page<final && !tmp_data_frame((struct nas_tmp_file *)file,page,false))page++;
    if(page==final)return 0;
    uint64_t first=page++;
    while(page<final && tmp_data_frame((struct nas_tmp_file *)file,page,false))page++;
    *logical=first*TMP_PAGE_SIZE;
    *length=(page-first)*TMP_PAGE_SIZE;
    /* A bounded query can end before later extents. The inode-owned
     * watermark covers all allocated data pages, including MAP_SHARED
     * faults, so every extent is found in one forward pass (not O(N^2)). */
    *last=(page>=file->highest_allocated_data_page_plus_one);
    return 1;
}
static uint8_t *tmp_data_page(struct nas_tmp_file *file,uint64_t number,
                              bool create) {
    uint64_t frame=tmp_data_frame(file,number,create);
    return frame ? (uint8_t *)(uintptr_t)(nas_hhdm_offset()+frame) : NULL;
}
/* A mmap writer may touch the mapped last page past i_size. Those bytes are
 * NOT file contents. Both pwrite into a later hole and ftruncate-grow must
 * zero newly exposed bytes of the old partial EOF page before publishing a
 * larger size. Other sparse pages already read as zero without allocation. */
static void tmp_zero_newly_visible_tail(struct nas_tmp_file *file,
                                        uint64_t old_size,uint64_t next_size) {
    if(next_size<=old_size || !(old_size%TMP_PAGE_SIZE))return;
    uint64_t end=(old_size/TMP_PAGE_SIZE+1)*TMP_PAGE_SIZE;
    if(end>next_size)end=next_size;
    uint8_t *tail=tmp_data_page(file,old_size/TMP_PAGE_SIZE,false);
    if(tail)memset(tail+old_size%TMP_PAGE_SIZE,0,(size_t)(end-old_size));
}
int tmp_acquire_shared_page(struct nas_tmp_file *file,uint64_t offset,
                            uint64_t *pa_owned) {
    if(!pa_owned)return -EINVAL;
    *pa_owned=0;
    if(!file || !file->used || file->is_dir)return -EINVAL;
    if(offset&(TMP_PAGE_SIZE-1))return -EINVAL;
    /* Fault beyond i_size is SIGBUS, not an implicit extension of the file.
     * A page overlapping EOF is still mapped; the logical size remains
     * unchanged until write(2)/ftruncate(2), just as in Linux tmpfs. */
    if(offset>=file->size)return -EFAULT;
    uint64_t frame=tmp_data_frame(file,offset/TMP_PAGE_SIZE,true);
    if(!frame)return -ENOMEM;
    /* The sparse index retains the storage owner; the caller gets an
     * independent PTE owner and must put it on failure/unmap. */
    if(!nas_frame_get(frame))return -EIO;
    *pa_owned=frame;
    return 0;
}
int tmp_touch_shared(struct nas_tmp_file *file) {
    if(!file || !file->used || file->is_dir)return -EINVAL;
    file->mtime_ns=tmp_next_mtime(file->mtime_ns);
    file->ctime_ns=file->mtime_ns;
    return 0;
}
int64_t tmp_read_at(const struct nas_tmp_file *const_file, uint64_t offset,
                    uint8_t *dst, uint64_t count) {
    if (!const_file || !const_file->used || (!dst && count)) return -EINVAL;
    /* A zero-byte read validates a regular inode without requiring a
     * destination buffer. In particular, MAP_PRIVATE validates its file
     * description with nas_fs_mmap_read(fd, offset, NULL, 0), even when
     * the underlying file is a writable tmpfs inode (Git config/index). */
    if (!count) return 0;
    if (offset >= const_file->size) return 0;
    if (count > const_file->size - offset) count = const_file->size - offset;
    uint64_t done = 0;
    /* A read does not allocate physical pages for sparse zero-filled holes. */
    struct nas_tmp_file *file = (struct nas_tmp_file *)(uintptr_t)const_file;
    while (done < count) {
        uint64_t at = offset + done;
        uint64_t length = TMP_PAGE_SIZE - at % TMP_PAGE_SIZE;
        if (length > count - done) length = count - done;
        uint8_t *page = tmp_data_page(file, at / TMP_PAGE_SIZE, false);
        if (page) memcpy(dst + done, page + at % TMP_PAGE_SIZE, (size_t)length);
        else memset(dst + done, 0, (size_t)length);
        done += length;
    }
    return (int64_t)done;
}
int64_t tmp_write_at(struct nas_tmp_file *file, uint64_t offset,
                     const uint8_t *src, uint64_t count) {
    if (!file || !file->used || !src || file->is_dir) return -EINVAL;
    if (!count) return 0;
    if (offset >= TMP_FILE_CAP) return -ENOSPC;
    if (count > TMP_FILE_CAP - offset) count = TMP_FILE_CAP - offset;
    bool has_hole=offset>file->size;
    uint64_t old_size=file->size;
    uint64_t done = 0;
    while (done < count) {
        uint64_t at = offset + done;
        uint64_t length = TMP_PAGE_SIZE - at % TMP_PAGE_SIZE;
        if (length > count - done) length = count - done;
        uint8_t *page = tmp_data_page(file, at / TMP_PAGE_SIZE, true);
        if (!page) return done ? (int64_t)done : -ENOMEM;
        /* Do not alter even a hidden mmap tail when the first destination
         * allocation fails and the write has made zero progress. */
        if(!done && has_hole)
            tmp_zero_newly_visible_tail(file,old_size,offset);
        memcpy(page + at % TMP_PAGE_SIZE, src + done, (size_t)length);
        done += length;
        if (at + length > file->size) file->size = at + length;
        file->mtime_ns = tmp_next_mtime(file->mtime_ns);
        file->ctime_ns = file->mtime_ns;
    }
    return (int64_t)done;
}
void tmp_truncate(struct nas_tmp_file *file) {
    if (!file) return;
    if (file->index_root) {
        uint64_t *root = (uint64_t *)(uintptr_t)(
            nas_hhdm_offset() + file->index_root);
        for (unsigned outer = 0; outer < TMP_INDEX_ROOT_ENTRIES; outer++) {
            uint64_t frame = root[outer];
            if (!frame) continue;
            uint64_t *table = (uint64_t *)(uintptr_t)(nas_hhdm_offset() + frame);
            for (unsigned inner = 0; inner < TMP_PAGE_ENTRIES; inner++) {
                if (table[inner]) (void)nas_frame_put(table[inner]);
            }
            (void)nas_frame_put(frame);
        }
        (void)nas_frame_put(file->index_root);
        file->index_root = 0;
    }
    file->highest_allocated_data_page_plus_one = 0;
    file->size = 0;
    if (file->used) {
        file->mtime_ns = tmp_next_mtime(file->mtime_ns);
        file->ctime_ns = file->mtime_ns;
    }
}
/* Linux ftruncate: grow as sparse zero-filled pages, shrink discards pages
 * AND zeroes the partial final page, so later regrowth cannot expose
 * truncated bytes. Inode identity and open-descriptor offset survive. */
int tmp_resize(struct nas_tmp_file *file, uint64_t size) {
    if (!file || !file->used) return -ENOENT;
    if (file->is_dir) return -EISDIR;
    if (size > TMP_FILE_CAP) return -EFBIG;
    uint64_t old = file->size;
    /* Linux updates mtime/ctime on a successful ftruncate even when the
     * requested size equals the current size.  O_TRUNC takes this path too:
     * an empty file reopened with O_TRUNC must acquire a fresh timestamp. */
    if (size < old) {
        if (size % TMP_PAGE_SIZE) {
            uint8_t *last = tmp_data_page(file, size / TMP_PAGE_SIZE, false);
            if (last) memset(last + size % TMP_PAGE_SIZE, 0,
                             (size_t)(TMP_PAGE_SIZE - size % TMP_PAGE_SIZE));
        }
        if (file->index_root) {
            uint64_t *root = (uint64_t *)(uintptr_t)(
                nas_hhdm_offset() + file->index_root);
            uint64_t first = (size + TMP_PAGE_SIZE - 1) / TMP_PAGE_SIZE;
            uint64_t oldpages = (old + TMP_PAGE_SIZE - 1) / TMP_PAGE_SIZE;
            for (uint64_t page = first; page < oldpages; page++) {
                unsigned outer = (unsigned)(page / TMP_PAGE_ENTRIES);
                uint64_t frame = root[outer];
                if (!frame) continue;
                uint64_t *slots = (uint64_t *)(uintptr_t)(
                    nas_hhdm_offset() + frame);
                unsigned inner = (unsigned)(page % TMP_PAGE_ENTRIES);
                if (slots[inner]) {
                    (void)nas_frame_put(slots[inner]);
                    slots[inner] = 0;
                }
            }
            bool root_has_index = false;
            uint64_t surviving_highest = 0;
            for (unsigned outer = 0; outer < TMP_INDEX_ROOT_ENTRIES; outer++) {
                uint64_t frame = root[outer];
                if (!frame) continue;
                uint64_t *slots = (uint64_t *)(uintptr_t)(
                    nas_hhdm_offset() + frame);
                bool any = false;
                /* This existing shrink sweep already visits every resident
                 * index frame; recover the high-water mark without a new
                 * O(file size) scan on each FIEMAP extent. */
                for (unsigned i = 0; i < TMP_PAGE_ENTRIES; i++)
                    if (slots[i]) {
                        any = true;
                        uint64_t after = (uint64_t)outer * TMP_PAGE_ENTRIES + i + 1;
                        if (after > surviving_highest) surviving_highest = after;
                    }
                if (!any) {
                    (void)nas_frame_put(frame);
                    root[outer] = 0;
                } else root_has_index = true;
            }
            if (!root_has_index) {
                (void)nas_frame_put(file->index_root);
                file->index_root = 0;
            }
            file->highest_allocated_data_page_plus_one = surviving_highest;
        } else {
            file->highest_allocated_data_page_plus_one = 0;
        }
    }
    tmp_zero_newly_visible_tail(file,old,size);
    file->size = size;
    file->mtime_ns = tmp_next_mtime(file->mtime_ns);
    file->ctime_ns = file->mtime_ns;
    return 0;
}
int tmp_utimens(struct nas_tmp_file *file, uint64_t access_ns,
                uint64_t modified_ns, bool set_access, bool set_modified) {
    if (!file || !file->used) return -ENOENT;
    if (set_access) file->atime_ns = access_ns;
    if (set_modified) file->mtime_ns = modified_ns;
    if (set_access || set_modified) file->ctime_ns =
        tmp_next_mtime(file->ctime_ns);
    return 0;
}

/* Path-to-inode bindings live in fs/tmp_namespace.c. */
bool tmp_is_link(const struct nas_tmp_file *file) {
    return file && (file->mode & 0170000U) == 0120000U;
}
/* A volatile symlink is a real tmpfs inode. Its literal target (including
 * relative path segments) is stored in the same bounded sparse-page data
 * backend as file data, so rename/unlink follow existing inode ownership. */
int tmp_link_target(const struct nas_tmp_file *file, char target[256]) {
    if (!tmp_is_link(file)) return -EINVAL;
    if (file->size >= 256) return -ENAMETOOLONG;
    int64_t copied = tmp_read_at(file, 0, (uint8_t *)target, file->size);
    if (copied < 0) return (int)copied;
    if ((uint64_t)copied != file->size) return -EIO;
    target[file->size] = 0;
    return 0;
}
/* Namespace owns symlink names and writable inode creation. */
/* Inode storage identity is independent from namespace name/link count.
 * The namespace owner publishes a dentry only AFTER successful allocation. */
struct nas_tmp_file *tmp_inode_alloc(bool dir,uint32_t mode) {
    if(!tmp_free_inodes && !tmp_new_slab())return 0;
    struct nas_tmp_file *file=tmp_free_inodes;
    tmp_free_inodes=file->free_next;
    *file=(struct nas_tmp_file){0};
    file->used=true;
    file->ino=tmp_next_ino++;
    file->mode=(dir?0040000U:0100000U)|(mode&07777U);
    file->is_dir=dir;
    file->atime_ns=file->mtime_ns=file->ctime_ns=tmp_now_ns();
    return file;
}
void tmp_inode_link(struct nas_tmp_file *file) {
    if(!file || !file->used)return;
    file->nlink++;
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
}
void tmp_inode_unlink(struct nas_tmp_file *file) {
    if(!file || !file->used || !file->nlink)return;
    file->nlink--;
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
    tmp_try_reclaim(file);
}
void tmp_inode_discard(struct nas_tmp_file *file) {
    tmp_try_reclaim(file);
}
void tmp_inode_touch(struct nas_tmp_file *file) {
    if(file && file->used)file->ctime_ns=tmp_next_mtime(file->ctime_ns);
}
uint64_t tmp_number(const struct nas_tmp_file *file) {
    return file ? file->ino : 0;
}
/* These are actual Linux mkdir(83), rmdir(84), unlink(87) and
 * mkdirat/unlinkat entry points, not shell-command path rewrites.
 * Read-only CPIO refuses mutations; only the /tmp overlay is writable.
 */
/* Inode mode is shared by chmod/fchmod, access and execve.
 * Preserve the file-type bits; CPIO remains strictly read-only. */
int tmp_chmod(struct nas_tmp_file *file, uint64_t mode) {
    if(!file || !file->used)return -ENOENT;
    file->mode=(file->mode & 0170000U) | (uint32_t)(mode & 07777U);
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
    return 0;
}
int tmp_chown(struct nas_tmp_file *file,uint32_t uid,uint32_t gid) {
    if(!file || !file->used)return -ENOENT;
    /* Linux (uid_t)-1 / (gid_t)-1 independently preserve current ownership.
     * A real change of ownership invalidates set-ID execution privileges. */
    const bool owner_changed=uid!=UINT32_MAX && uid!=file->uid;
    const bool group_changed=gid!=UINT32_MAX && gid!=file->gid;
    if(uid!=UINT32_MAX)file->uid=uid;
    if(gid!=UINT32_MAX)file->gid=gid;
    if(owner_changed || group_changed)file->mode &= ~06000U;
    file->ctime_ns=tmp_next_mtime(file->ctime_ns);
    return 0;
}
/* Namespace mutations are implemented by fs/tmp_namespace.c. */
