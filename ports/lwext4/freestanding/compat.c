#include "platform.h"
#include "include/string.h"
#include "include/stdlib.h"
#include "../../../kernel/mm/heap.h"
#include <stdint.h>
void *ext4_user_malloc(size_t n) {return nas_heap_alloc(n);}
void *ext4_user_calloc(size_t n,size_t size) {return nas_heap_calloc(n,size);}
void *ext4_user_realloc(void *p,size_t n) {return nas_heap_realloc(p,n);}
void ext4_user_free(void *p) {(void)nas_heap_free(p);}
static void exchange(unsigned char *a,unsigned char *b,size_t size) {
    while(size--) {unsigned char t=*a;*a++=*b;*b++=t;}
}
static void sift(unsigned char *base,size_t root,size_t end,size_t size,
                 int (*compare)(const void *,const void *)) {
    for(;;) {
        if(root>(end-1)/2 || !end)return;
        size_t child=root*2+1,chosen=root;
        if(compare(base+chosen*size,base+child*size)<0)chosen=child;
        if(child<end &&
           compare(base+chosen*size,base+(child+1)*size)<0)chosen=child+1;
        if(chosen==root)return;
        exchange(base+root*size,base+chosen*size,size);
        root=chosen;
    }
}
void qsort(void *ptr,size_t count,size_t size,
           int (*compare)(const void *,const void *)) {
    if(!ptr || !compare || count<2 || !size || count>SIZE_MAX/size)return;
    unsigned char *base=ptr;
    for(size_t start=(count-2)/2+1;start;) {
        --start;sift(base,start,count-1,size,compare);
    }
    for(size_t end=count-1;end;) {
        exchange(base,base+end*size,size);
        --end;sift(base,0,end,size,compare);
    }
}
