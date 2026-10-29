#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define INPUT_SELECT (1u << 0)
#define INPUT_START  (1u << 3)

#define INPUT_UP     (1u << 4)
#define INPUT_RIGHT  (1u << 5)
#define INPUT_DOWN   (1u << 6)
#define INPUT_LEFT   (1u << 7)

#define INPUT_L2     (1u << 8)
#define INPUT_R2     (1u << 9)

#define INPUT_L      (1u << 10)
#define INPUT_R      (1u << 11)

#define INPUT_X      (1u << 12)
#define INPUT_A      (1u << 13)
#define INPUT_B      (1u << 14)
#define INPUT_Y      (1u << 15)

int input_init(void);
uint32_t input_raw(void);
int input_pressed(uint32_t button);
void input_shutdown(void);
int input_check_health(void);
int input_reinit(void);

#endif