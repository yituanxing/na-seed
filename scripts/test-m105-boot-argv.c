#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/task/user_stack.h"

/* Link the ACTUAL production parser from kernel/task/user_stack.c. The
 * unrelated ELF/user-memory stack writer is section-GC'd by the runner. */
static void require(bool ok,const char *what) {
    if(ok)return;
    fprintf(stderr,"[M105 BOOT ARGV FAIL] %s\n",what);
    exit(1);
}

int main(void) {
    const char *const *args=0;
    unsigned argc=0;

    /* A real BusyBox `sh -c` command, including quotes, internal spaces,
     * and a semicolon, is a single argument after argv[0] and `-c`. */
    char script[760];
    memcpy(script,"printf ",7);
    memset(script+7,'x',560);
    memcpy(script+567,"; echo second",14);
    char line[900];
    snprintf(line,sizeof(line),"naseed.argv=/bin/busybox sh -c '%s'",script);
    require(strlen(line)>256,"fixture exercises old 256-byte ceiling");
    require(nas_parse_boot_argv(line,&args,&argc),"quoted sh -c >256 bytes");
    require(argc==4,"long original command retains argc");
    require(strcmp(args[0],"/bin/busybox")==0 &&
            strcmp(args[1],"sh")==0 && strcmp(args[2],"-c")==0 &&
            strcmp(args[3],script)==0,"long sh -c content is exact");
    require(nas_boot_argv_enabled() &&
            strcmp(nas_boot_argv_first(),"/bin/busybox")==0,
            "boot selector tracks successful long parse");
    puts("M105_BOOT_ARGV_LONG_QUOTED_PASS");

    const char quotes[] = "naseed.argv=/bin/echo \"two words\" "
        "'three words' slash\\ escaped 'quote\"same' \"quote'same\"";
    require(nas_parse_boot_argv(quotes,&args,&argc),"legacy quote parse");
    require(argc==6,"legacy quote arg count");
    require(strcmp(args[0],"/bin/echo")==0 &&
            strcmp(args[1],"two words")==0 &&
            strcmp(args[2],"three words")==0 &&
            strcmp(args[3],"slash escaped")==0 &&
            strcmp(args[4],"quote\"same")==0 &&
            strcmp(args[5],"quote'same")==0,"quote/backslash semantics unchanged");
    puts("M105_BOOT_ARGV_QUOTE_COMPAT_PASS");

    /* 4095 real bytes plus the mandatory NUL fit exactly in the 4096-byte
     * BSS store. The next byte must fail without a buffer overwrite. */
    static char boundary[4200];
    memcpy(boundary,"naseed.argv=",12);
    memset(boundary+12,'q',4095);
    boundary[12+4095]=0;
    require(nas_parse_boot_argv(boundary,&args,&argc),"exact BSS capacity");
    require(argc==1 && strlen(args[0])==4095,
            "exact-capacity argument preserved");
    boundary[12+4095]='q';
    boundary[12+4096]=0;
    require(!nas_parse_boot_argv(boundary,&args,&argc),
            "capacity+1 must fail, never truncate or overwrite");
    require(args==NULL && argc==0,
            "failed reparse also clears caller-visible argv and argc");
    require(!nas_boot_argv_enabled() && !nas_boot_argv_first(),
            "failed reparse cannot expose stale strings");
    puts("M105_BOOT_ARGV_BOUNDARY_PASS");

    require(!nas_parse_boot_argv("naseed.argv=/bin/sh 'unclosed",&args,&argc),
            "reject unterminated single quote");
    require(!nas_parse_boot_argv("naseed.argv=/bin/sh \\",&args,&argc),
            "reject trailing escape");
    require(!nas_parse_boot_argv("naseed.argv=/bin/sh \"line\nfeed\"",&args,&argc),
            "reject control bytes inside quotes");
    require(!nas_parse_boot_argv("invalid=/bin/sh",&args,&argc),
            "reject missing boot selector");
    require(!nas_parse_boot_argv("naseed.argv=",&args,&argc),
            "reject empty boot selector");
    require(!nas_parse_boot_argv(NULL,&args,&argc),"null boot metadata");
    require(!nas_parse_boot_argv("naseed.argv=/bin/sh",NULL,&argc),
            "null argv output must fail safely");
    require(!nas_boot_argv_enabled(),"invalid parse never leaves override");
    puts("M105_BOOT_ARGV_MALFORMED_PASS");
    puts("M105_BOOT_ARGV_HOST_PASS");
    return 0;
}
