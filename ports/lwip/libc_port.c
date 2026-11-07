/* Small freestanding string/decimal helpers required by official lwIP.
 * Not a second allocator, socket API, or copy of any protocol algorithm. */
#include <stddef.h>

int nas_lwip_atoi(const char *s) {
    if(!s)return 0;
    while(*s==' ' || *s=='\t' || *s=='\n' || *s=='\r')s++;
    int negative=0;
    if(*s=='+' || *s=='-')negative=(*s++=='-');
    unsigned value=0;
    while(*s>='0' && *s<='9') {
        unsigned digit=(unsigned)(*s++-'0');
        /* Saturate instead of overflow/UB when a malformed service or
         * interface suffix contains an arbitrarily long decimal string. */
        if(value>(2147483647U-digit)/10U)return negative ? -2147483647:2147483647;
        value=value*10U+digit;
    }
    return negative ? -(int)value:(int)value;
}

char *nas_lwip_strstr(const char *haystack,const char *needle) {
    if(!haystack || !needle)return NULL;
    if(!*needle)return (char *)haystack;
    for(const char *start=haystack;*start;start++) {
        const char *a=start,*b=needle;
        while(*a && *b && *a==*b){a++;b++;}
        if(!*b)return (char *)start;
    }
    return NULL;
}
