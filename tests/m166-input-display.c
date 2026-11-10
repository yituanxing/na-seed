#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Genuine Linux evdev userspace ABI: a hardware VirtIO input event changes
 * an application-owned framebuffer.  QMP independently samples the resulting
 * physical QEMU display; no guest-side marker is itself sufficient evidence.
 * This intentionally is an application, not a kernel compositor or driver.
 */
_Static_assert(sizeof(struct input_event) == 24, "Linux LP64 evdev ABI");
_Static_assert(sizeof(struct fb_var_screeninfo) == 160, "Linux LP64 fb ABI");
_Static_assert(sizeof(struct fb_fix_screeninfo) == 80, "Linux LP64 fb ABI");

enum { DEVICE_LIMIT = 16, PIXELS = 512, BAND = 128 };
struct input_handles {
    int keyboard;
    int tablet;
    int mouse;
    char keyboard_path[40];
};

static void fail(const char *where) {
    fprintf(stderr, "[M166 APP FAIL] %s errno=%d\n", where, errno);
    exit(17);
}
static void mark(const char *name) { puts(name); fflush(stdout); }

static int bit(const uint8_t *set, unsigned code, unsigned bytes) {
    return code / 8 < bytes && (set[code / 8] & (1U << (code % 8))) != 0;
}

static int has_code(int fd, unsigned type, unsigned code) {
    uint8_t codes[128] = {0};
    if (code / 8 >= sizeof(codes)) fail("input capability exceeds bitmap");
    if (ioctl(fd, EVIOCGBIT(type, sizeof(codes)), codes) < 0)
        fail("EVIOCGBIT capability query");
    return bit(codes, code, sizeof(codes));
}

static struct input_handles open_input(void) {
    struct input_handles handles = {.keyboard = -1, .tablet = -1,
                                    .mouse = -1};
    for (unsigned n = 0; n < DEVICE_LIMIT; ++n) {
        char path[40];
        snprintf(path, sizeof(path), "/dev/input/event%u", n);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno == ENOENT || errno == ENODEV) continue;
            fail("open evdev character device");
        }
        uint8_t types[8] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(types)), types) < 0)
            fail("EVIOCGBIT input device event types");
        char name[128] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
            fail("EVIOCGNAME actual VirtIO device");
        const int keys = bit(types, EV_KEY, sizeof(types));
        const int abs = bit(types, EV_ABS, sizeof(types));
        const int rel = bit(types, EV_REL, sizeof(types));
        if (keys && handles.keyboard < 0 && has_code(fd, EV_KEY, KEY_A)) {
            handles.keyboard = fd;
            snprintf(handles.keyboard_path, sizeof(handles.keyboard_path),
                     "%s", path);
        } else if (abs && handles.tablet < 0 &&
                 has_code(fd, EV_ABS, ABS_X) &&
                 has_code(fd, EV_ABS, ABS_Y) &&
                 has_code(fd, EV_KEY, BTN_LEFT))
            handles.tablet = fd;
        else if (rel && handles.mouse < 0 &&
                 has_code(fd, EV_REL, REL_X) && has_code(fd, EV_REL, REL_Y))
            handles.mouse = fd;
        else if (close(fd)) fail("close other input device");
    }
    if (handles.keyboard < 0 || handles.tablet < 0 || handles.mouse < 0)
        fail("three distinct real keyboard/tablet/mouse evdev devices");
    mark("M166_THREE_INPUT_DEVICES_PASS");
    return handles;
}

static void draw_row(int framebuffer, unsigned stage) {
    static const uint8_t colors[5][4][3] = {
        {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}},
        {{255, 255, 255}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}},
        {{255, 255, 255}, {255, 0, 255}, {0, 0, 255}, {255, 255, 255}},
        {{255, 255, 255}, {255, 0, 255}, {255, 255, 0}, {255, 255, 255}},
        {{255, 255, 255}, {255, 0, 255}, {255, 255, 0}, {0, 255, 255}},
    };
    if (stage >= 5) fail("invalid application frame stage");
    uint8_t bgra[PIXELS * 4];
    for (unsigned x = 0; x < PIXELS; ++x) {
        const uint8_t *rgb = colors[stage][x / BAND];
        bgra[x * 4 + 0] = rgb[2];
        bgra[x * 4 + 1] = rgb[1];
        bgra[x * 4 + 2] = rgb[0];
        bgra[x * 4 + 3] = 255;
    }
    if (lseek(framebuffer, 0, SEEK_SET) != 0)
        fail("seek hardware framebuffer");
    if (write(framebuffer, bgra, sizeof(bgra)) != (ssize_t)sizeof(bgra))
        fail("write hardware framebuffer");
}

static int open_fb(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) fail("open /dev/fb0");
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0)
        fail("query real framebuffer format");
    if (v.xres < PIXELS || v.yres < 1 || v.bits_per_pixel != 32 ||
        v.red.offset != 16 || v.green.offset != 8 ||
        v.blue.offset != 0 || f.line_length < PIXELS * 4)
        fail("unsupported QEMU display contract");
    return fd;
}

static void original_elf(const char *path, char *const args[]) {
    pid_t pid = fork();
    if (pid < 0) fail("fork signed Alpine ELF");
    if (pid == 0) {
        execv(path, args);
        fail("exec signed Alpine ELF");
    }
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        fail("signed Alpine ELF execution failed");
}

static void signed_evtest_key_state(const char *device, int pressed) {
    pid_t pid = fork();
    if (pid < 0) fail("fork original signed evtest");
    if (pid == 0) {
        char *argv[] = {"evtest", "--query", (char *)device,
                        "EV_KEY", "KEY_A", NULL};
        execv("/usr/bin/evtest", argv);
        fail("exec original signed Alpine evtest");
    }
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
        WEXITSTATUS(status) != (pressed ? 10 : 0))
        fail("real signed evtest key-state ioctl query");
    mark(pressed ? "M166_SIGNED_EVTEST_DOWN_PASS" :
                    "M166_SIGNED_EVTEST_UP_PASS");
}

static void original_fb_verification(void) {
    char *grab[] = {"fbgrab", "-d", "/dev/fb0", "/tmp/m166-screen.png", NULL};
    original_elf("/usr/bin/fbgrab", grab);
    int fd = open("/tmp/m166-screen.png", O_RDONLY);
    const uint8_t expected[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    uint8_t actual[8];
    if (fd < 0 || read(fd, actual, 8) != 8 ||
        memcmp(actual, expected, 8) != 0)
        fail("original fbgrab PNG after actual keyboard and mouse input");
    if (close(fd)) fail("close fbgrab image");
    mark("M166_ORIGINAL_FBGRAB_PASS");
    char *sample[] = {"magick", "/tmp/m166-screen.png", "-crop",
                      "1x1+416+0", "+repage", "-depth", "8",
                      "RGB:/tmp/m166-sampled.rgb", NULL};
    original_elf("/usr/bin/magick", sample);
    fd = open("/tmp/m166-sampled.rgb", O_RDONLY);
    uint8_t rgb[3];
    if (fd < 0 || read(fd, rgb, 3) != 3 ||
        rgb[0] != 0 || rgb[1] != 255 || rgb[2] != 255)
        fail("original ImageMagick true cyan sample");
    if (close(fd)) fail("close signed magick RGB sample");
    mark("M166_ORIGINAL_MAGICK_PASS");
}

int main(void) {
    struct input_handles in = open_input();
    int fb = open_fb();
    draw_row(fb, 0);
    mark("M166_FRAME_INITIAL_VISIBLE");
    struct pollfd fds[3] = {{in.keyboard, POLLIN, 0},
                            {in.tablet, POLLIN, 0},
                            {in.mouse, POLLIN, 0}};
    unsigned stage = 0;
    int abs_x = 0, abs_y = 0, rel_x = 0, rel_y = 0;
    int signed_up_checked = 0;
    while (stage < 5) {
        int ready = poll(fds, 3, 30000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            fail("poll real evdev input");
        }
        if (!ready) fail("timed out awaiting actual QEMU input event");
        for (unsigned i = 0; i < 3; ++i) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                fail("evdev poll error or device disconnect");
            if (!(fds[i].revents & POLLIN)) continue;
            struct input_event events[32];
            ssize_t n = read(fds[i].fd, events, sizeof(events));
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (n <= 0 || (n % (ssize_t)sizeof(events[0])) != 0)
                fail("read real aligned LP64 input_event records");
            for (size_t j = 0; j < (size_t)n / sizeof(events[0]); ++j) {
                const struct input_event *e = &events[j];
                if (stage == 0 && i == 0 && e->type == EV_KEY &&
                    e->code == KEY_A && e->value == 1) {
                    draw_row(fb, ++stage);
                    signed_evtest_key_state(in.keyboard_path, 1);
                    mark("M166_FRAME_KEY_VISIBLE");
                } else if (stage == 1 && i == 0 && e->type == EV_KEY &&
                           e->code == KEY_A && e->value == 0 &&
                           !signed_up_checked) {
                    signed_evtest_key_state(in.keyboard_path, 0);
                    signed_up_checked = 1;
                } else if (stage == 1 && i == 1 && e->type == EV_ABS) {
                    if (e->code == ABS_X && e->value > 1000) abs_x = 1;
                    if (e->code == ABS_Y && e->value > 1000) abs_y = 1;
                } else if (stage == 1 && i == 1 &&
                           e->type == EV_SYN && e->code == SYN_REPORT &&
                           abs_x && abs_y) {
                    draw_row(fb, ++stage);
                    mark("M166_FRAME_ABS_VISIBLE");
                } else if (stage == 2 && (i == 1 || i == 2) &&
                           e->type == EV_KEY &&
                           e->code == BTN_LEFT && e->value == 1) {
                    /* QEMU selects the active pointer handler for button
                     * events when both tablet and relative mouse exist.
                     * Either is an independently enumerated real VirtIO
                     * device; ABS and REL are validated on their own. */
                    draw_row(fb, ++stage);
                    mark("M166_FRAME_CLICK_VISIBLE");
                } else if (stage == 3 && i == 2 && e->type == EV_REL) {
                    if (e->code == REL_X && e->value > 0) rel_x = 1;
                    if (e->code == REL_Y && e->value > 0) rel_y = 1;
                } else if (stage == 3 && i == 2 &&
                           e->type == EV_SYN && e->code == SYN_REPORT &&
                           rel_x && rel_y) {
                    draw_row(fb, ++stage);
                    mark("M166_FRAME_REL_VISIBLE");
                } else if (stage == 4 && i == 0 && e->type == EV_KEY &&
                           e->code == KEY_Q && e->value == 1) {
                    ++stage;
                }
            }
        }
    }
    if (close(in.keyboard) || close(in.tablet) || close(in.mouse) || close(fb))
        fail("close all input and framebuffer descriptors");
    original_fb_verification();
    mark("M166_REAL_EVDEV_DISPLAY_PASS");
    return 0;
}
