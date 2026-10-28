/* Ported original jkent/ramfs uses the common NaSeed heap, not another
 * allocator. Upstream sources remain byte-for-byte unchanged. */
#include "../../kernel/mm/heap.h"
#include "../../kernel/lib/string.h"
#include <stddef.h>
int nas_ramfs_errno;
void nas_ramfs_free(void *p) {(void)nas_heap_free(p);}
char *nas_ramfs_strdup(const char *p) {
    if(!p)return NULL;
    size_t n=strlen(p);
    char *v=nas_heap_alloc(n+1);
    if(v)memcpy(v,p,n+1);
    return v;
}
char *nas_ramfs_strndup(const char *p,size_t max) {
    if(!p)return NULL;
    size_t n=strnlen(p,max);
    char *v=nas_heap_alloc(n+1);
    if(v){memcpy(v,p,n);v[n]=0;}
    return v;
}
