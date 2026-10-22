#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/lib/string.h"

static void ck(int yes, const char *message) {
    if (!yes) { fprintf(stderr, "[KLIB FAIL] %s\n", message); exit(1); }
}
static void reference_copy(unsigned char *d, const unsigned char *s, size_t n) {
    for (size_t i=0; i<n; i++) d[i]=s[i];
}
static void reference_move(unsigned char *d, const unsigned char *s, size_t n) {
    if ((uintptr_t)d > (uintptr_t)s && (uintptr_t)d-(uintptr_t)s < n) {
        for (size_t i=n; i>0; --i) d[i-1]=s[i-1];
    } else reference_copy(d,s,n);
}
static void equal_bytes(const unsigned char *a, const unsigned char *b,
                        size_t n, const char *message) {
    for (size_t i=0; i<n; i++) ck(a[i] == b[i],message);
}
int main(void) {
    unsigned char a[512], b[512], r[512];
    for (size_t len=0;len<192;len++) {
        for (size_t off=0;off<16;off++) {
            for (size_t i=0;i<512;i++) {
                a[i]=(unsigned char)(i*53U+len);
                b[i]=0xA5;
                r[i]=0xA5;
            }
            ck(memcpy(b+64+off,a+off,len)==b+64+off,"memcpy return");
            reference_copy(r+64+off,a+off,len);
            equal_bytes(b,r,512,"memcpy data/canaries");
            ck(memcmp(b,r,512)==0,"memcmp equality");
            ck(memset(b+64+off,0x1A5,len)==b+64+off,"memset return");
            for(size_t i=0;i<len;i++)r[64+off+i]=0xA5;
            equal_bytes(b,r,512,"memset truncated byte/canaries");
            for(size_t i=0;i<512;i++)b[i]=r[i]=(unsigned char)(i*37U+3U);
            ck(memmove(b+64+off,b+off,len)==b+64+off,"memmove forward return");
            reference_move(r+64+off,r+off,len);
            equal_bytes(b,r,512,"memmove forward/canaries");
            for(size_t i=0;i<512;i++)b[i]=r[i]=(unsigned char)(i*37U+3U);
            ck(memmove(b+off,b+64+off,len)==b+off,"memmove backward return");
            reference_move(r+off,r+64+off,len);
            equal_bytes(b,r,512,"memmove backward/canaries");
            for(size_t i=0;i<512;i++)b[i]=r[i]=(unsigned char)(i*37U+3U);
            ck(memmove(b+off,b+off+1,len)==b+off,"memmove overlap left return");
            reference_move(r+off,r+off+1,len);
            equal_bytes(b,r,512,"memmove overlap left");
            for(size_t i=0;i<512;i++)b[i]=r[i]=(unsigned char)(i*37U+3U);
            ck(memmove(b+off+1,b+off,len)==b+off+1,"memmove overlap right return");
            reference_move(r+off+1,r+off,len);
            equal_bytes(b,r,512,"memmove overlap right");
        }
    }
    ck(memcmp("", "", 0)==0,"memcmp zero");
    ck(memcmp("\x80", "\x7f",1)>0,"memcmp unsigned bytes");
    ck(memcmp("\x7f", "\x80",1)<0,"memcmp unsigned bytes reverse");
    ck(strlen("")==0 && strlen("hello")==5,"strlen");
    ck(strnlen("hello",0)==0 && strnlen("hello",3)==3 &&
       strnlen("hello",9)==5,"strnlen bound");
    ck(strcmp("hello","hello")==0 && strcmp("a","ab")<0 &&
       strcmp("ab","a")>0,"strcmp");
    ck(strcmp("\x80","\x7f")>0,"strcmp unsigned bytes");
    ck(strncmp("a","b",0)==0 && strncmp("ax","ay",1)==0 &&
       strncmp("ax","ay",2)<0 && strncmp("ab","a",4)>0,"strncmp");
    const char *word="bananas";
    ck(strchr(word,'a')==word+1 && strchr(word,'z')==NULL &&
       strchr(word,0)==word+7,"strchr (including NUL)");
    ck(strrchr(word,'a')==word+5 && strrchr(word,'z')==NULL &&
       strrchr(word,0)==word+7,"strrchr (including NUL)");
    puts("[KLIB PASS] 10 primitives; alignment, overlap, bounds and unsigned compare");
    return 0;
}
