#ifndef WINDOWS_H
#define WINDOWS_H

#include "rendering.h"
#include "types.h"
#include "utils/font.h"

#define WINDOW_BORDER 8

#define WINDOW_TOP_LEFT      (rect2d){{0, 0}, {8, 8}}
#define WINDOW_TOP_MIDDLE    (rect2d){{8, 0}, {8, 8}}
#define WINDOW_TOP_RIGHT     (rect2d){{16, 0}, {8, 8}}
#define WINDOW_MIDDLE_LEFT   (rect2d){{0, 8}, {8, 8}}
#define WINDOW_MIDDLE_MIDDLE (rect2d){{8, 8}, {8, 8}}
#define WINDOW_MIDDLE_RIGHT  (rect2d){{16, 8}, {8, 8}}
#define WINDOW_BOTTOM_LEFT   (rect2d){{0, 16}, {8, 8}}
#define WINDOW_BOTTOM_MIDDLE (rect2d){{8, 16}, {8, 8}}
#define WINDOW_BOTTOM_RIGHT  (rect2d){{16, 16}, {8, 8}}
#define MOUSE_CURSOR         (rect2d){{32, 8}, {8, 8}}

#define WINDOW_CLOSE_BUTTON    (rect2d){{32, 0}, {8, 8}}
#define WINDOW_MINIMIZE_BUTTON (rect2d){{40, 0}, {8, 8}}

int windows_init(void);
void windows_cleanup(void);
void windows_handle_pointer(point2d pos, int pressed, int released);
void windows_handle_keyboard(uint32_t keycode, uint8_t down);
void windows_handle_resize(int window_id, rect2d bounds);
void windows_draw(pixmap *dst, font *f, pixmap *atlas);
int window_create_for_client(int client_fd, rect2d bounds, char *title, uint32_t *window_id);
void window_destroy(int window_id);
int window_destroy_for_client(int client_fd, uint32_t window_id);
int window_owned_by_client(int client_fd, uint32_t window_id);
void windows_destroy_all_for_client(int client_fd);
#endif
