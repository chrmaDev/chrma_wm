#ifndef CWM_SERVER_H
#define CWM_SERVER_H

#include <poll.h>
#include <stdint.h>
void startServer(void);
int protocol_fd(void);
int protocol_add_pollfds(struct pollfd *dst, int cap);
int process_new_connections(void);
int process_connections(void);

void process_get_info(int client_socket);
int process_create_window(int client_socket);
int process_destroy_window(int client_socket);
int process_commit_window(int client_socket);
int cwm_emit_input_event(int client_socket, uint8_t type, uint32_t keycode, uint8_t down, uint8_t button, uint16_t x, uint16_t y);
#endif
