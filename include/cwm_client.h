#ifndef CWM_CLIENT_H
#define CWM_CLIENT_H

#include "cwm_protocol.h"
#include "rendering.h"

/*
 * connect, create_window, pixmap/pixels, draw, commit.
 * poll client_fd, drain poll_event (resize: pixmap again, close: exit).
 * destroy_window, disconnect.
 */
int cwm_client_connect(void);
int cwm_client_disconnect(void);

int cwm_request_create_window(const char *title, int width, int height);
int cwm_request_destroy_window(int window_id);
int cwm_request_commit_window(int window_id);
int cwm_request_get_info(struct get_info_response *out);
int cwm_client_fd(void);
int cwm_client_poll_event(struct input_event *out);

void *cwm_client_pixels(void);
int cwm_client_width(void);
int cwm_client_height(void);
int cwm_client_stride(void);
void cwm_client_pixmap(pixmap *out);

#endif
