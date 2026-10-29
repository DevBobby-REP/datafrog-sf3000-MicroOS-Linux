#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

#define DRAW_WIDTH  854
#define DRAW_HEIGHT 480

#define RGB565(r, g, b) \
    ((uint16_t)((((r) >> 3) << 11) | \
                (((g) >> 2) << 5)  | \
                ((b) >> 3)))

int draw_init(void);
void draw_clear(uint16_t color);
void draw_pixel(int x, int y, uint16_t color);
uint16_t* draw_get_buffer(void);
void draw_present(void);
void draw_shutdown(void);
void draw_blit_rgb565(const void* src, int width, int height, int pitch);
int  draw_fb0_init(void);
void draw_fb0_blit(const void* src, int width, int height, int pitch);
void draw_fb0_finish(void);

#endif