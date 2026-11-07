#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* A real Linux syscall-ABI application, built for all three native ISAs.
 * Physical host serial bytes traverse architecture UART/SBI -> shared TTY
 * -> POSIX read(0) -> actual firmware /dev/fb0 writes. Original signed
 * fbgrab and ImageMagick independently decode the final hardware pixels.
 * No test-specific kernel API, kernel drawing, fake input or device mmap. */
_Static_assert(sizeof(struct fb_var_screeninfo) == 160, "Linux LP64 var ABI");
_Static_assert(sizeof(struct fb_fix_screeninfo) == 80, "Linux LP64 fix ABI");

static void fail(const char *where) {
    fprintf(stderr, "[M164 APP FAIL] %s errno=%d\n", where, errno);
    exit(17);
}

static void mark(const char *label) {
    puts(label);
    fflush(stdout);
}

static void read_command(char *buffer, size_t capacity) {
    ssize_t size = read(STDIN_FILENO, buffer, capacity - 1);
    if (size <= 0 || (size_t)size >= capacity) fail("physical TTY read");
    buffer[size] = '\0';
}

static void draw_line(int fd, int swapped) {
    static const uint8_t primary[4][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}
    };
    uint8_t bgra[512 * 4];
    for (unsigned x = 0; x < 512; ++x) {
        const uint8_t *rgb = primary[swapped ? 3 - x / 128 : x / 128];
        bgra[4 * x + 0] = rgb[2];
        bgra[4 * x + 1] = rgb[1];
        bgra[4 * x + 2] = rgb[0];
        bgra[4 * x + 3] = 255;
    }
    if (lseek(fd, 0, SEEK_SET) != 0) fail("fbdev seek");
    if (write(fd, bgra, sizeof(bgra)) != (ssize_t)sizeof(bgra))
        fail("fbdev physical write");
}

static void child_program(const char *name, char *const argv[]) {
    pid_t child = fork();
    if (child < 0) fail("fork original ELF");
    if (child == 0) {
        execv(name, argv);
        fail("execv original ELF");
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) fail("original ELF child status");
}

int main(void) {
    if (ioctl(STDIN_FILENO, TIOCSCTTY, 0) != 0)
        fail("real controlling UART TIOCSCTTY");
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) != 0) fail("TTY termios");
    term.c_lflag |= ICANON;
    term.c_iflag |= ICRNL;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) != 0)
        fail("TTY canonical input setup");

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) fail("open real framebuffer");
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0)
        fail("query actual display mode");
    if (var.xres < 512 || var.yres < 1 || var.bits_per_pixel != 32 ||
        var.red.offset != 16 || var.green.offset != 8 ||
        var.blue.offset != 0 || fix.line_length < 2048)
        fail("unexpected native framebuffer pixel contract");

    draw_line(fd, 0);
    mark("M164_FRAME_A_VISIBLE");
    char input[32];
    read_command(input, sizeof(input));
    if (strcmp(input, "wrong\n") != 0) fail("first actual keyboard command");
    /* An invalid command is rejected without modifying the display. */
    mark("M164_INVALID_INPUT_REJECTED");
    read_command(input, sizeof(input));
    if (strcmp(input, "flip\n") != 0) fail("real keyboard flip command");
    draw_line(fd, 1);
    mark("M164_FRAME_B_VISIBLE");
    read_command(input, sizeof(input));
    if (strcmp(input, "quit\n") != 0) fail("real keyboard quit command");
    if (close(fd) != 0) fail("fbdev close");

    char *grab[] = {"fbgrab", "-d", "/dev/fb0", "/tmp/m164-screen.png", NULL};
    child_program("/usr/bin/fbgrab", grab);
    int picture = open("/tmp/m164-screen.png", O_RDONLY);
    uint8_t signature[8];
    static const uint8_t png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (picture < 0 || read(picture, signature, 8) != 8 ||
        memcmp(signature, png, 8) != 0) fail("original fbgrab real PNG");
    close(picture);
    mark("M164_FBGRAB_PNG_PASS");

    /* A second original Alpine executable decodes captured screen pixels;
     * QMP separately samples the physical QEMU display, not this file. */
    char *convert[] = {"magick", "/tmp/m164-screen.png", "-crop",
                       "1x1+32+0", "+repage", "-depth", "8",
                       "RGB:/tmp/m164-sampled.rgb", NULL};
    child_program("/usr/bin/magick", convert);
    int sample = open("/tmp/m164-sampled.rgb", O_RDONLY);
    uint8_t rgb[3];
    if (sample < 0 || read(sample, rgb, 3) != 3 ||
        rgb[0] != 255 || rgb[1] != 255 || rgb[2] != 255)
        fail("original ImageMagick captured white pixel");
    close(sample);
    mark("M164_MAGICK_PIXEL_PASS");
    mark("M164_INTERACTIVE_PASS");
    return 0;
}
