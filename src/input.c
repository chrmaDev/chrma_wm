#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include "utils/vector.h"

struct key_event {
    uint32_t keycode;
    uint8_t down;
};

enum sticky_mode { STICKY_OFF, STICKY_LATCHED, STICKY_LOCKED };

struct sticky_modifier {
    uint32_t keycode;
    uint32_t active_for;
    enum sticky_mode mode;
    uint8_t physical_down;
    uint8_t used;
};

static struct {
    struct udev *udev;
    struct libinput *li;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    Vector key_events;
    pointer_state pointer;
    double scroll_remainder_x;
    double scroll_remainder_y;
    struct sticky_modifier sticky[3];
    int quit;
    int spawn;
    int screenshot;
} g;

int button_pressed(uint8_t button)
{
    return (g.pointer.buttons & button) && !(g.pointer.prev_buttons & button);
}

int button_released(uint8_t button)
{
    return !(g.pointer.buttons & button) && (g.pointer.prev_buttons & button);
}

void input_consume_buttons(void)
{
    g.pointer.prev_buttons = g.pointer.buttons;
}

static int open_restricted(const char *path, int flags, void *user_data)
{
    int fd;

    (void)user_data;
    fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data)
{
    (void)user_data;
    close(fd);
}

static const struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void handle_motion(struct libinput_event *event)
{
    struct libinput_event_pointer *pt = libinput_event_get_pointer_event(event);

    g.pointer.dx += (int)libinput_event_pointer_get_dx(pt);
    g.pointer.dy += (int)libinput_event_pointer_get_dy(pt);
}

static void handle_button(struct libinput_event *event)
{
    struct libinput_event_pointer *pt = libinput_event_get_pointer_event(event);
    uint32_t button = libinput_event_pointer_get_button(pt);
    enum libinput_button_state state = libinput_event_pointer_get_button_state(pt);
    uint8_t bit;

    g.pointer.prev_buttons = g.pointer.buttons;

    if (button == BTN_LEFT)
        bit = 1;
    else if (button == BTN_RIGHT)
        bit = 2;
    else if (button == BTN_MIDDLE)
        bit = 4;
    else
        return;

    if (state == LIBINPUT_BUTTON_STATE_PRESSED)
        g.pointer.buttons |= bit;
    else if (state == LIBINPUT_BUTTON_STATE_RELEASED)
        g.pointer.buttons &= (uint8_t)~bit;
}

static void handle_scroll(struct libinput_event *event)
{
    struct libinput_event_pointer *pt = libinput_event_get_pointer_event(event);
    enum libinput_event_type type = libinput_event_get_type(event);
    const enum libinput_pointer_axis axes[2] = {
        LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
        LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
    };
    double *remainders[2] = { &g.scroll_remainder_x, &g.scroll_remainder_y };
    int16_t *steps[2] = { &g.pointer.scroll_x, &g.pointer.scroll_y };

    for (int i = 0; i < 2; i++) {
        double movement;
        int whole;

        if (!libinput_event_pointer_has_axis(pt, axes[i]))
            continue;
        if (type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL)
            movement = libinput_event_pointer_get_scroll_value_v120(pt, axes[i]) / 120.0;
        else
            movement = libinput_event_pointer_get_scroll_value(pt, axes[i]) / 20.0;
        *remainders[i] -= movement; /* libinput: down/right positive; raylib: up/left positive */
        whole = (int)*remainders[i];
        *remainders[i] -= whole;
        *steps[i] += (int16_t)whole;
    }
}

static void queue_key(uint32_t keycode, uint8_t down)
{
    struct key_event event = { keycode, down };
    vector_push(&g.key_events, &event);
}

static int sticky_index(xkb_keysym_t sym)
{
    if (sym == XKB_KEY_Shift_L || sym == XKB_KEY_Shift_R) return 0;
    if (sym == XKB_KEY_Control_L || sym == XKB_KEY_Control_R) return 1;
    if (sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R) return 2;
    return -1;
}

static void handle_key(struct libinput_event *event)
{
    struct libinput_event_keyboard *kb = libinput_event_get_keyboard_event(event);
    uint32_t keycode = libinput_event_keyboard_get_key(kb) + 8;
    enum libinput_key_state ks = libinput_event_keyboard_get_key_state(kb);
    enum xkb_key_direction dir = (ks == LIBINPUT_KEY_STATE_PRESSED) ? XKB_KEY_DOWN : XKB_KEY_UP;
    xkb_keysym_t sym;
    int modifier;

    xkb_state_update_key(g.xkb_state, keycode, dir);
    sym = xkb_state_key_get_one_sym(g.xkb_state, keycode);
    modifier = sticky_index(sym);

    if (modifier >= 0) {
        struct sticky_modifier *sticky = &g.sticky[modifier];
        if (dir == XKB_KEY_DOWN) {
            sticky->physical_down = 1;
            sticky->used = 0;
            sticky->keycode = keycode;
        }
        queue_key(keycode, dir == XKB_KEY_DOWN);
        if (dir == XKB_KEY_UP) {
            sticky->physical_down = 0;
            if (!sticky->used)
                sticky->mode = (enum sticky_mode)((sticky->mode + 1) % 3);
        }
        return;
    }

    if (dir == XKB_KEY_DOWN) {
        for (int i = 0; i < 3; i++) {
            struct sticky_modifier *sticky = &g.sticky[i];
            if (sticky->physical_down) sticky->used = 1;
            if (sticky->mode != STICKY_OFF && !sticky->physical_down && !sticky->active_for) {
                queue_key(sticky->keycode, 1);
                sticky->active_for = keycode;
            }
        }
    }

    if (sym == XKB_KEY_BackSpace
        && xkb_state_mod_name_is_active(g.xkb_state, XKB_MOD_NAME_CTRL,
                                        XKB_STATE_MODS_EFFECTIVE) > 0
        && xkb_state_mod_name_is_active(g.xkb_state, XKB_MOD_NAME_ALT,
                                        XKB_STATE_MODS_EFFECTIVE) > 0) {
        if (ks == LIBINPUT_KEY_STATE_PRESSED)
            g.quit = 1;
        goto finish;
    }
    if (sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R) {
        if (ks == LIBINPUT_KEY_STATE_PRESSED)
            g.spawn = 1;
        goto finish;
    }
    if (sym == XKB_KEY_F12 || sym == XKB_KEY_Print) {
        if (ks == LIBINPUT_KEY_STATE_PRESSED)
            g.screenshot = 1;
        goto finish;
    }

    queue_key(keycode, dir == XKB_KEY_DOWN);

finish:
    if (dir == XKB_KEY_UP) {
        for (int i = 0; i < 3; i++) {
            struct sticky_modifier *sticky = &g.sticky[i];
            if (sticky->active_for != keycode) continue;
            queue_key(sticky->keycode, 0);
            sticky->active_for = 0;
            if (sticky->mode == STICKY_LATCHED) sticky->mode = STICKY_OFF;
        }
    }
}

static void handle_device_added(struct libinput_event *event)
{
    struct libinput_device *dev = libinput_event_get_device(event);

    if (libinput_device_config_tap_get_finger_count(dev) > 0)
        libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
}

int input_init(void)
{
    memset(&g, 0, sizeof(g));
    vector_init(&g.key_events, sizeof(struct key_event));
    g.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!g.xkb_ctx)
        return -1;
    g.keymap = xkb_keymap_new_from_names(g.xkb_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!g.keymap) {
        input_cleanup();
        return -1;
    }
    g.xkb_state = xkb_state_new(g.keymap);
    if (!g.xkb_state) {
        input_cleanup();
        return -1;
    }

    g.udev = udev_new();
    if (!g.udev) {
        input_cleanup();
        return -1;
    }
    g.li = libinput_udev_create_context(&interface, NULL, g.udev);
    if (!g.li) {
        input_cleanup();
        return -1;
    }
    if (libinput_udev_assign_seat(g.li, "seat0") < 0) {
        input_cleanup();
        return -1;
    }

    libinput_dispatch(g.li);
    return 0;
}

void input_cleanup(void)
{
    vector_free(&g.key_events);
    if (g.li)
        libinput_unref(g.li);
    if (g.udev)
        udev_unref(g.udev);
    if (g.xkb_state)
        xkb_state_unref(g.xkb_state);
    if (g.keymap)
        xkb_keymap_unref(g.keymap);
    if (g.xkb_ctx)
        xkb_context_unref(g.xkb_ctx);
    memset(&g, 0, sizeof(g));
}

int input_fd(void)
{
    return g.li ? libinput_get_fd(g.li) : -1;
}

int input_dispatch(void)
{
    struct libinput_event *event;

    if (!g.li)
        return -1;
    if (libinput_dispatch(g.li) < 0)
        return -1;

    while ((event = libinput_get_event(g.li)) != NULL) {
        switch (libinput_event_get_type(event)) {
        case LIBINPUT_EVENT_POINTER_MOTION:
            handle_motion(event);
            break;
        case LIBINPUT_EVENT_POINTER_BUTTON:
            handle_button(event);
            break;
        case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
        case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
        case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
            handle_scroll(event);
            break;
        case LIBINPUT_EVENT_KEYBOARD_KEY:
            handle_key(event);
            break;
        case LIBINPUT_EVENT_DEVICE_ADDED:
            handle_device_added(event);
            break;
        default:
            break;
        }
        libinput_event_destroy(event);
    }
    return 0;
}

pointer_state input_pointer(void)
{
    return g.pointer;
}

void input_consume_motion(void)
{
    g.pointer.dx = 0;
    g.pointer.dy = 0;
    g.pointer.scroll_x = 0;
    g.pointer.scroll_y = 0;
}

int input_quit_requested(void)
{
    return g.quit;
}

int input_spawn_requested(void)
{
    int spawn = g.spawn;
    g.spawn = 0;
    return spawn;
}

int input_screenshot_requested(void)
{
    int screenshot = g.screenshot;
    g.screenshot = 0;
    return screenshot;
}

int input_next_key_event(uint32_t *keycode, uint8_t *down)
{
    struct key_event *event;

    if (g.key_events.size == 0)
        return 0;
    event = vector_get(&g.key_events, 0);
    *keycode = event->keycode;
    *down = event->down;
    vector_remove(&g.key_events, 0);
    return 1;
}
