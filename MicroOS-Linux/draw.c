#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "draw.h"

typedef int  (*video_init_t)(void);
typedef void (*video_deinit_t)(void);
typedef int  (*video_disp_t)(void*, int, int, int);

static void* driver = NULL;

static video_init_t  video_init = NULL;
static video_deinit_t video_deinit = NULL;
static video_disp_t  video_disp = NULL;
static int fb0_fd = -1;
static uint16_t* fb0_mem = NULL;
static int fb0_w = 0, fb0_h = 0, fb0_stride = 0;
static long fb0_size = 0;


static uint16_t* framebuffer = NULL;


int draw_init(void)
{
    driver = dlopen(
        "/mnt/sdcard/cubegm/driver.so",
        RTLD_NOW | RTLD_GLOBAL
    );

    if (!driver)
        return -1;

    video_init = (video_init_t)dlsym(
        driver,
        "video_drivers_init"
    );

    video_deinit = (video_deinit_t)dlsym(
        driver,
        "video_driver_deinit"
    );

    video_disp = (video_disp_t)dlsym(
        driver,
        "video_driver_disp_frame"
    );

    if (!video_init ||
        !video_deinit ||
        !video_disp) {

        dlclose(driver);
        driver = NULL;

        return -1;
    }

    if (video_init() <= 0) {

        dlclose(driver);
        driver = NULL;

        return -1;
    }

    framebuffer = malloc(
        DRAW_WIDTH *
        DRAW_HEIGHT *
        sizeof(uint16_t)
    );

    if (!framebuffer) {

        video_deinit();
        dlclose(driver);

        driver = NULL;

        return -1;
    }

    memset(
        framebuffer,
        0,
        DRAW_WIDTH *
        DRAW_HEIGHT *
        sizeof(uint16_t)
    );

    return 0;
}


void draw_clear(uint16_t color)
{
    int i;

    if (!framebuffer)
        return;

    for (i = 0;
        i < DRAW_WIDTH * DRAW_HEIGHT;
        i++) {

        framebuffer[i] = color;
    }
}


void draw_pixel(
    int x,
    int y,
    uint16_t color
)
{
    if (!framebuffer)
        return;

    if (x < 0 ||
        y < 0 ||
        x >= DRAW_WIDTH ||
        y >= DRAW_HEIGHT)
        return;

    framebuffer[
        y * DRAW_WIDTH + x
    ] = color;
}


uint16_t* draw_get_buffer(void)
{
    return framebuffer;
}

static uint32_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000UL + tv.tv_usec / 1000UL);
}

void draw_present(void)
{
    if (!video_disp || !framebuffer)
        return;

    static uint32_t last_push_time = 0;
    uint32_t now = get_time_ms();
	
    if (now - last_push_time < 16) {
        return; 
    }

    last_push_time = now;

    video_disp(
        framebuffer,
        DRAW_WIDTH,
        DRAW_HEIGHT,
        DRAW_WIDTH * 2
    );
}

void draw_blit_rgb565(const void* src, int width, int height, int pitch)
{
    uint16_t* fb = draw_get_buffer();
    if (!fb || !src || width <= 0 || height <= 0)
        return;

    const uint16_t* s = (const uint16_t*)src;
    int sp = pitch / 2;

    static int xmap[DRAW_WIDTH];
    static int ymap[DRAW_HEIGHT];

    static int last_width = 0;
    static int last_height = 0;

    if (width != last_width) {
        for (int x = 0; x < DRAW_WIDTH; x++)
            xmap[x] = x * width / DRAW_WIDTH;

        last_width = width;
    }

    if (height != last_height) {
        for (int y = 0; y < DRAW_HEIGHT; y++)
            ymap[y] = y * height / DRAW_HEIGHT;

        last_height = height;
    }

    for (int y = 0; y < DRAW_HEIGHT; y++) {
        const uint16_t* src_row =
            s + (size_t)ymap[y] * sp;

        uint16_t* dst_row =
            fb + (size_t)y * DRAW_WIDTH;

        for (int x = 0; x < DRAW_WIDTH; x++) {
            dst_row[x] = src_row[xmap[x]];
        }
    }
}

int draw_fb0_init(void) {
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;

    fb0_fd = open("/dev/fb0", O_RDWR);
    if (fb0_fd < 0) return -1;

    ioctl(fb0_fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fb0_fd, FBIOGET_FSCREENINFO, &fi);

    fb0_w = vi.xres;
    fb0_h = vi.yres;
    fb0_stride = fi.line_length / 2;
    fb0_size = fi.smem_len;

    fb0_mem = mmap(NULL, fb0_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb0_fd, 0);
    if (fb0_mem == MAP_FAILED) {
        fb0_mem = NULL;
        close(fb0_fd);
        fb0_fd = -1;
        return -1;
    }

    fprintf(stderr, "draw_fb0_init: %dx%d stride=%d\n", fb0_w, fb0_h, fb0_stride);
    return 0;
}

void draw_fb0_blit(const void* src, int width, int height, int pitch) {
    if (!fb0_mem || !src) return;

    const int sp = pitch / 2;
    const uint16_t* s = (const uint16_t*)src;

    static int symap[2048];
    static int last_h = -1, last_fbw = -1;
    if (height != last_h || fb0_w != last_fbw) {
        for (int dx = 0; dx < fb0_w; dx++)
            symap[dx] = (height - 1) - (dx * height / fb0_w);
        last_h = height;
        last_fbw = fb0_w;
    }

    for (int dy = 0; dy < fb0_h; dy++) {
        int sx = dy * width / fb0_h;
        uint16_t* drow = fb0_mem + (size_t)dy * fb0_stride;
        for (int dx = 0; dx < fb0_w; dx++)
            drow[dx] = s[(size_t)symap[dx] * sp + sx];
    }
}

void draw_fb0_finish(void) {
    if (fb0_mem) { munmap(fb0_mem, fb0_size); fb0_mem = NULL; }
    if (fb0_fd >= 0) { close(fb0_fd); fb0_fd = -1; }
}


void draw_shutdown(void)
{
    if (framebuffer) {

        free(framebuffer);
        framebuffer = NULL;
    }

    if (video_deinit) {

        video_deinit();
        video_deinit = NULL;
    }

    video_init = NULL;
    video_disp = NULL;

    if (driver) {

        dlclose(driver);
        driver = NULL;
    }
}