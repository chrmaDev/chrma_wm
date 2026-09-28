#ifndef CWM_PROTOCOL_H
#define CWM_PROTOCOL_H

#include <stdint.h>
#include "types.h"
#define CWM_PROTOCOL_VERSION 2
#define CWM_SOCKET_PATH "/tmp/cwm.sock"
#define CWM_MAX_WINDOW_DIMENSION 4096
#define CWM_MAX_WINDOW_COORDINATE 32767

enum cwmInputEventType {
    CWM_INPUT_EVENT_TYPE_MOUSE = 0,
    CWM_INPUT_EVENT_TYPE_KEYBOARD = 1,
    CWM_INPUT_EVENT_TYPE_RESIZE = 2,
    CWM_INPUT_EVENT_TYPE_MOUSE_MOVE = 3,
    CWM_INPUT_EVENT_TYPE_MOUSE_WHEEL = 4,
    CWM_INPUT_EVENT_TYPE_FOCUS = 5,
    CWM_INPUT_EVENT_TYPE_WINDOW_STATE = 6,
};

enum cwmWindowState {
    CWM_WINDOW_RESTORE = 0,
    CWM_WINDOW_MINIMIZE = 1,
    CWM_WINDOW_MAXIMIZE = 2,
    CWM_WINDOW_FULLSCREEN = 3,
};

#define CWM_CLIPBOARD_MAX 1024

enum cwmRenderScale {
    CWM_RENDER_NATIVE = 0,
    CWM_RENDER_SCALED = 1,
};

enum cwmProtocolMessageType {
    CWM_PRT_TYPE_GET_INFO = 1,
    CWM_PRT_TYPE_CREATE_WINDOW = 2,
    CWM_PRT_TYPE_DESTROY_WINDOW = 3,
    CWM_PRT_TYPE_COMMIT_WINDOW = 4,
    CWM_PRT_TYPE_CREATE_WINDOW_REPLY = 5,
    CWM_PRT_TYPE_INPUT_EVENT = 6,
    CWM_PRT_TYPE_RESIZE_WINDOW = 7,
    CWM_PRT_TYPE_CLOSE_WINDOW = 8,
    CWM_PRT_TYPE_SET_WINDOW_TITLE = 9,
    CWM_PRT_TYPE_SET_WINDOW_SIZE = 10,
    CWM_PRT_TYPE_SET_WINDOW_STATE = 11,
    CWM_PRT_TYPE_SET_CLIPBOARD = 12,
    CWM_PRT_TYPE_CLIPBOARD_UPDATE = 13,
};

/* Wire: type:u8, length:u32be, payload[length]. One SEQPACKET record. */
#define CWM_HDR_SIZE 5

/* RESIZE_WINDOW payload: window_id, width, height as u32be. New memfd in SCM_RIGHTS. */
#define CWM_RESIZE_PAYLOAD 12

struct cwm_msg_hdr {
    uint8_t type;
    uint32_t length;
};

int cwm_send(int socket, int type, const void *payload, uint32_t length);
int cwm_send_fd(int socket, int type, const void *payload, uint32_t length, int fd);

struct get_info_response {
    point2d physical_resolution;
    point2d logical_resolution;
    uint32_t colour_depth;
    uint32_t refresh_rate;
};

struct window_create_request {
    rect2d rect;
    char title[256];
    enum cwmRenderScale render_scale;
};

struct input_event {
    uint8_t type;
    uint8_t down;
    uint8_t button;
    uint32_t keycode;
    uint16_t x;
    uint16_t y;
};

#endif
