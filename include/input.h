#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define BUTTON_LEFT 1
#define BUTTON_RIGHT 2
#define BUTTON_MIDDLE 4

typedef struct {
    int dx;
    int dy;
    uint8_t buttons; /* bit0 left, bit1 right, bit2 middle */
    uint8_t prev_buttons;
    int16_t scroll_x;
    int16_t scroll_y;
} pointer_state;

int input_init(void);
void input_cleanup(void);
int input_fd(void);
int input_dispatch(void);
pointer_state input_pointer(void);
void input_consume_motion(void);
void input_consume_buttons(void);
int input_quit_requested(void);
int input_spawn_requested(void);
int input_screenshot_requested(void);
int input_next_key_event(uint32_t *keycode, uint8_t *down);

int button_pressed(uint8_t button);
int button_released(uint8_t button);
#endif
