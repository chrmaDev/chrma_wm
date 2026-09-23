#include "cwm_client.h"
#include "cwm_protocol.h"

#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void fill_solid(uint8_t *px, int w, int h, int stride, uint8_t b, uint8_t g, uint8_t r)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t *p = px + (size_t)y * stride + (size_t)x * 4;
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = 255;
        }
    }
}

int main(void)
{
    uint8_t *px;
    int w, h, stride, window_id;
    uint8_t b = 40, g = 180, r = 220;
    struct pollfd pfd;

    if (cwm_client_connect() < 0)
        return 1;
    window_id = cwm_request_create_window("Window", 100, 80);
    if (window_id < 0)
        return 1;

    px = cwm_client_pixels();
    w = cwm_client_width();
    h = cwm_client_height();
    stride = cwm_client_stride();
    if (!px)
        return 1;

    fill_solid(px, w, h, stride, b, g, r);
    if (cwm_request_commit_window(window_id) < 0)
        return 1;

    pfd.fd = cwm_client_fd();
    pfd.events = POLLIN;
    pfd.revents = 0;
    while (poll(&pfd, 1, -1) > 0) {
        struct input_event ev;
        int got;

        while ((got = cwm_client_poll_event(&ev)) > 0) {
            switch (ev.type) {
            case CWM_PRT_TYPE_CLOSE_WINDOW:
                goto done;
            case CWM_INPUT_EVENT_TYPE_RESIZE:
                px = cwm_client_pixels();
                w = cwm_client_width();
                h = cwm_client_height();
                stride = cwm_client_stride();
                if (!px)
                    return 1;
                fill_solid(px, w, h, stride, b, g, r);
                if (cwm_request_commit_window(window_id) < 0)
                    return 1;
                break;
            case CWM_INPUT_EVENT_TYPE_KEYBOARD:
            case CWM_INPUT_EVENT_TYPE_MOUSE:
                if (!ev.down)
                    break;
                b = (uint8_t)(b + 32);
                fill_solid(px, w, h, stride, b, g, r);
                if (cwm_request_commit_window(window_id) < 0)
                    return 1;
                break;
            default:
                break;
            }
        }
        if (got < 0)
            break;
    }

done:
    cwm_client_disconnect();
    return 0;
}
