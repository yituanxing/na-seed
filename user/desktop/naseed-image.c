#define _GNU_SOURCE
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Independently exec'ed image client; original signed Alpine ImageMagick
 * decodes/resizes the staged PPM into its private shared-memory surface. */
static void original_image_program(int channel) {
    pid_t pid=fork();nasd_require(pid>=0,"fork signed original image converter");
    if(!pid) {
        /* A third-party image converter must never inherit the desktop IPC
         * capability even though its parent is a trusted desktop client. */
        close(channel);
        char *const argv[]={"magick","/tests/m167-desktop-fixture.ppm",
                           "-resize","300x220!","-depth","8",
                           "RGB:/tmp/m167-image.rgb",NULL};
        execv("/usr/bin/magick",argv);
        _exit(127);
    }
    int status=0;
    nasd_require(waitpid(pid,&status,0)==pid && WIFEXITED(status) &&
            WEXITSTATUS(status)==0,"signed ImageMagick generator failed");
}
static void image_client(int channel) {
    int surface=-1;
    uint8_t *pixels=nasd_make_surface("m167-image",IMAGE_W,IMAGE_H,&surface);
    original_image_program(channel);
    int rgb=open("/tmp/m167-image.rgb",O_RDONLY);
    nasd_require(rgb>=0,"original ImageMagick RGB output missing");
    uint8_t row[IMAGE_W*3];
    for(unsigned y=0;y<IMAGE_H;y++) {
        nasd_read_all(rgb,row,sizeof(row));
        for(unsigned x=0;x<IMAGE_W;x++) {
            unsigned source=x*3;
            size_t dst=((size_t)y*IMAGE_W+x)*4U;
            pixels[dst+0]=row[source+2];pixels[dst+1]=row[source+1];
            pixels[dst+2]=row[source+0];pixels[dst+3]=255;
        }
    }
    uint8_t excess=0;
    ssize_t trailing;
    do trailing=read(rgb,&excess,1);
    while(trailing<0 && errno==EINTR);
    nasd_require(trailing==0,"signed magick output byte size");
    nasd_require(!close(rgb),"close ImageMagick output");
    nasd_require(pixels[0]==255 && pixels[1]==0 && pixels[2]==255 &&
            pixels[((size_t)IMAGE_W*IMAGE_H-1)*4U+2]==255,
            "signed image expected magenta pixel contract");
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nasd_send_surface_caps(channel,surface,IMAGE,IMAGE_W,IMAGE_H,0);
    nasd_mark("M167_SIGNED_MAGICK_IMAGE_PASS");
    for(;;) {
        struct message packet=nasd_recv_packet(channel);
        if(packet.type==MSG_QUIT)break;
        nasd_require(0,"unexpected image client message");
    }
    munmap(pixels,(size_t)IMAGE_W*IMAGE_H*4U);
    close(surface);close(channel);
}

int main(void) {
    image_client(nasd_client_channel());
    return 0;
}
