#include "cwm_protocol.h"
#include "cwm_server.h"
#include "kms.h"
#include "windows.h"
#include "utils/vector.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static struct {
    int socket;
    Vector client_connections;
    char *recv_buffer;
    size_t recv_buffer_size;
    size_t recv_len;
} server = { .socket = -1 };

static uint32_t uint32_from_buffer(const char *buffer)
{
    const unsigned char *b = (const unsigned char *)buffer;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void process_message(int client_socket);

int cwm_send(int socket, int type, const void *payload, uint32_t length)
{
    return cwm_send_fd(socket, type, payload, length, -1);
}

int cwm_send_fd(int socket, int type, const void *payload, uint32_t length, int fd)
{
    uint8_t hdr[CWM_HDR_SIZE];
    struct iovec iov[2];
    int iovlen = 1;
    struct msghdr msg;
    union {
        struct cmsghdr align;
        char buf[CMSG_SPACE(sizeof(int))];
    } cmsgbuf;
    struct cmsghdr *cmsg;

    if (length > 0 && !payload)
        return -1;

    hdr[0] = (uint8_t)type;
    put_u32be(hdr + 1, length);

    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = hdr;
    iov[0].iov_len = CWM_HDR_SIZE;
    if (length > 0) {
        iov[1].iov_base = (void *)payload;
        iov[1].iov_len = length;
        iovlen = 2;
    }
    msg.msg_iov = iov;
    msg.msg_iovlen = iovlen;

    if (fd >= 0) {
        memset(&cmsgbuf, 0, sizeof(cmsgbuf));
        msg.msg_control = cmsgbuf.buf;
        msg.msg_controllen = sizeof(cmsgbuf.buf);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    }

    if (sendmsg(socket, &msg, 0) < 0) {
        perror("sendmsg");
        return -1;
    }
    return 0;
}

void startServer(void)
{
    struct sockaddr_un addr;

    server.socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (server.socket == -1) {
        perror("socket");
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CWM_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(CWM_SOCKET_PATH);
    if (bind(server.socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server.socket);
        server.socket = -1;
        return;
    }
    if (listen(server.socket, 10) == -1) {
        perror("listen");
        close(server.socket);
        server.socket = -1;
        return;
    }
    fcntl(server.socket, F_SETFL, O_NONBLOCK);

    vector_init(&server.client_connections, sizeof(int));
    server.recv_buffer = malloc(2048);
    server.recv_buffer_size = 2048;
}

int protocol_fd(void)
{
    return server.socket;
}

int protocol_add_pollfds(struct pollfd *dst, int cap)
{
    int n = 0;
    size_t i;

    if (cap < 1)
        return 0;
    if (server.socket >= 0 && n < cap) {
        dst[n].fd = server.socket;
        dst[n].events = POLLIN;
        dst[n].revents = 0;
        n++;
    }
    for (i = 0; i < server.client_connections.size && n < cap; i++) {
        dst[n].fd = *(int *)vector_get(&server.client_connections, i);
        dst[n].events = POLLIN;
        dst[n].revents = 0;
        n++;
    }
    return n;
}

int process_new_connections(void)
{
    int accepted = 0;

    if (server.socket < 0)
        return 0;

    for (;;) {
        int c = accept(server.socket, NULL, NULL);
        if (c == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("accept error");
            break;
        }
        fcntl(c, F_SETFL, O_NONBLOCK);
        vector_push(&server.client_connections, &c);
        printf("new client connected: %d\n", c);
        accepted++;
    }
    return accepted;
}

int process_connections(void)
{
    size_t i = 0;
    int work = 0;

    while (i < server.client_connections.size) {
        int c = *(int *)vector_get(&server.client_connections, i);
        ssize_t bytes_read = recv(c, server.recv_buffer, server.recv_buffer_size, 0);

        if (bytes_read > 0) {
            server.recv_len = (size_t)bytes_read;
            process_message(c);
            work = 1;
            continue;
        }
        if (bytes_read == 0 ||
            (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            printf("client disconnected: %d\n", c);
            close(c);
            windows_destroy_all_for_client(c);
            vector_remove(&server.client_connections, i);
            work = 1;
            continue;
        }
        i++;
    }
    return work;
}

static void process_message(int client_socket)
{
    switch ((enum cwmProtocolMessageType)server.recv_buffer[0]) {
        case CWM_PRT_TYPE_GET_INFO:
            process_get_info(client_socket);
            break;

        case CWM_PRT_TYPE_CREATE_WINDOW: {
            process_create_window(client_socket);
            break;
        }

        case CWM_PRT_TYPE_DESTROY_WINDOW:
            process_destroy_window(client_socket);
            break;

        case CWM_PRT_TYPE_COMMIT_WINDOW:
            process_commit_window(client_socket);
            break;

        case CWM_PRT_TYPE_CREATE_WINDOW_REPLY:
        case CWM_PRT_TYPE_INPUT_EVENT:
        case CWM_PRT_TYPE_RESIZE_WINDOW:
        case CWM_PRT_TYPE_CLOSE_WINDOW:
            printf("unexpected message type from client: %d\n", server.recv_buffer[0]);
            break;

        default:
            printf("unknown message type: %d\n", server.recv_buffer[0]);
            break;
    }
}

void process_get_info(int client_socket)
{
    struct get_info_response response;
    response.physical_resolution = (point2d){get_width(), get_height()};
    response.logical_resolution = (point2d){get_width() / get_render_scale(), get_height() / get_render_scale()};
    response.colour_depth = 32; // TODO: get from KMS
    response.refresh_rate = get_refresh_rate();

    cwm_send(client_socket, CWM_PRT_TYPE_GET_INFO, &response, sizeof(response));
}

int process_create_window(int client_socket) {
    struct window_create_request request;
    uint32_t window_id;
    uint8_t idbuf[4];
    int gfx_fd;
    int title_length = 0;

    request.rect = (rect2d){{uint32_from_buffer(server.recv_buffer + 1), uint32_from_buffer(server.recv_buffer + 5)}, {uint32_from_buffer(server.recv_buffer + 9), uint32_from_buffer(server.recv_buffer + 13)}};
    for (size_t i = 17; i < server.recv_len && title_length < (int)sizeof(request.title) - 1; i++) {
        if (server.recv_buffer[i] == '\0') {
            request.title[title_length] = '\0';
            break;
        }
        request.title[title_length] = server.recv_buffer[i];
        title_length++;
    }
    request.title[title_length] = '\0';
    
    request.render_scale = (enum cwmRenderScale)uint32_from_buffer(server.recv_buffer + 17 + strlen(request.title) + 1);

    gfx_fd = window_create_for_client(client_socket, request.rect, request.title, &window_id);
    if (gfx_fd < 0)
        return -1;
    put_u32be(idbuf, window_id);
    if (cwm_send_fd(client_socket, CWM_PRT_TYPE_CREATE_WINDOW_REPLY, idbuf, 4, gfx_fd) < 0)
        return -1;
    return 0;
}

int process_destroy_window(int client_socket)
{
    uint32_t window_id;

    (void)client_socket;
    if (server.recv_len < 5)
        return -1;
    window_id = uint32_from_buffer(server.recv_buffer + 1);
    window_destroy((int)window_id);
    return 0;
}

int process_commit_window(int client_socket)
{
    (void)client_socket;
    if (server.recv_len < 5)
        return -1;
    return 0;
}

int cwm_emit_input_event(int client_socket, uint8_t type, uint32_t keycode, uint8_t down, uint8_t button, uint16_t x, uint16_t y)
{
    struct input_event event;

    event.type = type;
    event.down = down;
    event.button = button;
    event.keycode = keycode;
    event.x = x;
    event.y = y;
    return cwm_send(client_socket, CWM_PRT_TYPE_INPUT_EVENT, &event, sizeof(event));
}