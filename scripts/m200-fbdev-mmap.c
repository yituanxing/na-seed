/* M200 minimal native-Linux-ABI pixel witness. Run as the single initramfs
 * userspace ELF on amd64, riscv64 and arm64; no Alpine/OpenRC compositor is
 * present to race for /dev/fb0. This uses an actual MAP_SHARED user VMA,
 * never a read/write emulation of mmap. The independent QMP checker alone
 * establishes whether the pixels reached the physical VirtIO-GPU scanout. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define W 800U
#define H 600U
#define SAMPLE_Y 20U

struct sample { unsigned x; uint8_t r,g,b; };
static const struct sample samples[] = {
    {32U,255U,0U,0U}, {160U,0U,255U,0U},
    {288U,0U,0U,255U}, {416U,255U,255U,255U}
};

static int failed(const char *reason) {
    fprintf(stderr,"[M200 FBDEV MMAP FAIL] %s errno=%d (%s)\n",
            reason,errno,strerror(errno));
    fflush(stderr);
    return 200;
}
static size_t pixel_offset(const struct fb_fix_screeninfo *fix,unsigned x) {
    return (size_t)SAMPLE_Y*fix->line_length+(size_t)x*4U;
}
static void set_sample(volatile uint8_t *map,size_t offset,
                       const struct sample *s) {
    /* Packed truecolor B8,G8,R8,X8, exactly as the validated fbdev mode. */
    map[offset+0U]=s->b;
    map[offset+1U]=s->g;
    map[offset+2U]=s->r;
    map[offset+3U]=0U;
}
static int check_sample(const uint8_t *got,const struct sample *s) {
    return got[0]==s->b && got[1]==s->g &&
           got[2]==s->r && got[3]==0U;
}
static int check_mapped_sample(const volatile uint8_t *got,
                               const struct sample *s) {
    return got[0]==s->b && got[1]==s->g &&
           got[2]==s->r && got[3]==0U;
}

int main(void) {
    int fd=open("/dev/fb0",O_RDWR);
    if(fd<0)return failed("open /dev/fb0 O_RDWR");
    struct fb_var_screeninfo var={0};
    struct fb_fix_screeninfo fix={0};
    if(ioctl(fd,FBIOGET_VSCREENINFO,&var)<0 ||
       ioctl(fd,FBIOGET_FSCREENINFO,&fix)<0)
        return failed("Linux FBIOGET_{V,F}SCREENINFO");
    if(var.xres!=W || var.yres!=H || var.bits_per_pixel!=32U ||
       var.red.offset!=16U || var.green.offset!=8U ||
       var.blue.offset!=0U || var.red.length!=8U ||
       var.green.length!=8U || var.blue.length!=8U ||
       fix.line_length<W*4U || fix.smem_len<fix.line_length*H ||
       fix.type!=FB_TYPE_PACKED_PIXELS || fix.visual!=FB_VISUAL_TRUECOLOR)
        return failed("unexpected physical 800x600 BGRA fbdev mode");
    const size_t bytes=fix.smem_len;
    volatile uint8_t *map=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(map==MAP_FAILED)return failed("mmap /dev/fb0 MAP_SHARED RW");
    puts("M200_FBIOGET_AND_SHARED_MMAP_PASS");
    fflush(stdout);

    /* The fd's OpenFile pin must cease, but the independent VMA owner must
     * keep the exact DMA-backed DisplayDevice alive. */
    if(close(fd)!=0)return failed("close fbdev fd after mmap");
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
        if(i==1U)continue; /* The child will write this shared pixel. */
        size_t at=pixel_offset(&fix,samples[i].x);
        if(at>bytes-4U)return failed("sample outside framebuffer");
        set_sample(map,at,&samples[i]);
    }
    pid_t child=fork();
    if(child<0)return failed("fork MAP_SHARED framebuffer VMA");
    if(child==0) {
        size_t at=pixel_offset(&fix,samples[1].x);
        if(at>bytes-4U)_exit(201);
        set_sample(map,at,&samples[1]);
        _exit(0);
    }
    int status=0;
    if(waitpid(child,&status,0)!=child || !WIFEXITED(status) ||
       WEXITSTATUS(status)!=0)
        return failed("child shared framebuffer mapping write");

    /* Independently reopen fbdev and read the physical backing bytes, not
     * the same user mapping, to verify GPU-owned frame identity. */
    fd=open("/dev/fb0",O_RDONLY);
    if(fd<0)return failed("reopen /dev/fb0 after mmap fd close");
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
        uint8_t actual[4]={0};
        size_t at=pixel_offset(&fix,samples[i].x);
        if(at>bytes-4U ||
           !check_mapped_sample(map+at,&samples[i]) ||
           pread(fd,actual,sizeof(actual),(off_t)at)!=(ssize_t)sizeof(actual) ||
           !check_sample(actual,&samples[i]))
            return failed("MAP_SHARED fork visibility or fbdev pread mismatch");
    }
    if(close(fd)!=0)return failed("close post-map readback fd");
    puts("M200_DEVICE_FORK_AND_FD_CLOSE_PASS");
    puts("M200_SCREEN_DRAWN");
    fflush(stdout);

    /* QMP starts only after SCREEN_DRAWN and retries actual GPU frames.
     * Holding the mapping long enough avoids an exit-vs-screendump race. */
    sleep(10);
    if(munmap((void *)map,bytes)!=0)return failed("munmap /dev/fb0");
    puts("M200_FB_MMAP_GUEST_PASS");
    fflush(stdout);
    return 0;
}
