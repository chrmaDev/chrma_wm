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
#include <sys/stat.h>
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

static int process_message(int client_socket);

static int clear_stale_socket(const struct sockaddr_un *addr)
{
    struct stat st;
    int probe;
    int error;

    if (lstat(CWM_SOCKET_PATH, &st) < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISSOCK(st.st_mode) || st.st_uid != geteuid()) {
        errno = EADDRINUSE;
        return -1;
    }
    probe = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (probe < 0)
        return -1;
    if (connect(probe, (const struct sockaddr *)addr, sizeof(*addr)) == 0) {
        close(probe);
        errno = EADDRINUSE;
        return -1;
    }
    error = errno;
    close(probe);
    if (error != ECONNREFUSED) {
        errno = error;
        return -1;
    }
    return unlink(CWM_SOCKET_PATH);
}

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

    if (sendmsg(socket, &msg, MSG_NOSIGNAL) < 0) {
        perror("sendmsg");
        return -1;
    }
    return 0;
}

int startServer(void)
{
    struct sockaddr_un addr;

    server.socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (server.socket == -1) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CWM_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (clear_stale_socket(&addr) < 0) {
        perror("socket path");
        close(server.socket);
        server.socket = -1;
        return -1;
    }
    if (bind(server.socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server.socket);
        server.socket = -1;
        return -1;
    }
    if (listen(server.socket, 10) == -1) {
        perror("listen");
        close(server.socket);
        server.socket = -1;
        unlink(CWM_SOCKET_PATH);
        return -1;
    }
    fcntl(server.socket, F_SETFL, O_NONBLOCK);

    if (vector_init(&server.client_connections, sizeof(int)) < 0) {
        protocol_cleanup();
        return -1;
    }
    server.recv_buffer = malloc(2048);
    if (!server.recv_buffer) {
        protocol_cleanup();
        return -1;
    }
    server.recv_buffer_size = 2048;
    return 0;
}

void protocol_cleanup(void)
{
    for (size_t i = 0; i < server.client_connections.size; i++)
        close(*(int *)vector_get(&server.client_connections, i));
    vector_free(&server.client_connections);
    free(server.recv_buffer);
    server.recv_buffer = NULL;
    server.recv_buffer_size = 0;
    if (server.socket >= 0) {
        close(server.socket);
        server.socket = -1;
        unlink(CWM_SOCKET_PATH);
    }
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
        if (server.client_connections.size >= CWM_MAX_PFDS - 3 ||
            vector_push(&server.client_connections, &c) < 0) {
            close(c);
            continue;
        }
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
        ssize_t bytes_read = recv(c, server.recv_buffer, server.recv_buffer_size, MSG_TRUNC);

        if (bytes_read > 0) {
            server.recv_len = (size_t)bytes_read;
            if (server.recv_len > server.recv_buffer_size ||
                process_message(c) < 0) {
                close(c);
                windows_destroy_all_for_client(c);
                vector_remove(&server.client_connections, i);
                work = 1;
                continue;
            }
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

static int process_message(int client_socket)
{
    switch ((enum cwmProtocolMessageType)server.recv_buffer[0]) {
        case CWM_PRT_TYPE_GET_INFO:
            if (server.recv_len != 1)
                return -1;
            process_get_info(client_socket);
            return 0;

        case CWM_PRT_TYPE_CREATE_WINDOW:
            return process_create_window(client_socket);

        case CWM_PRT_TYPE_DESTROY_WINDOW:
            return process_destroy_window(client_socket);

        case CWM_PRT_TYPE_COMMIT_WINDOW:
            return process_commit_window(client_socket);

        case CWM_PRT_TYPE_CREATE_WINDOW_REPLY:
        case CWM_PRT_TYPE_INPUT_EVENT:
        case CWM_PRT_TYPE_RESIZE_WINDOW:
        case CWM_PRT_TYPE_CLOSE_WINDOW:
            printf("unexpected message type from client: %d\n", server.recv_buffer[0]);
            return -1;

        default:
            printf("unknown message type: %d\n", server.recv_buffer[0]);
            return -1;
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
    uint32_t x, y, width, height;
    uint8_t idbuf[4];
    int gfx_fd;
    size_t title_length;
    const char *title_end;

    if (server.recv_len < 22)
        return -1;
    title_end = memchr(server.recv_buffer + 17, '\0', server.recv_len - 17 - 4);
    if (!title_end)
        return -1;
    title_length = (size_t)(title_end - (server.recv_buffer + 17));
    if (title_length >= sizeof(request.title) || server.recv_len != 17 + title_length + 1 + 4)
        return -1;
    memcpy(request.title, server.recv_buffer + 17, title_length);
    request.title[title_length] = '\0';
    request.render_scale = (enum cwmRenderScale)uint32_from_buffer(title_end + 1);
    if (request.render_scale != CWM_RENDER_NATIVE)
        return -1;

    width = uint32_from_buffer(server.recv_buffer + 9);
    height = uint32_from_buffer(server.recv_buffer + 13);
    x = uint32_from_buffer(server.recv_buffer + 1);
    y = uint32_from_buffer(server.recv_buffer + 5);
    if (!width || !height || width > CWM_MAX_WINDOW_DIMENSION ||
        height > CWM_MAX_WINDOW_DIMENSION ||
        x > CWM_MAX_WINDOW_COORDINATE || y > CWM_MAX_WINDOW_COORDINATE)
        return -1;
    request.rect = (rect2d){{(int32_t)x, (int32_t)y},
                            {(int32_t)width, (int32_t)height}};

    gfx_fd = window_create_for_client(client_socket, request.rect, request.title, &window_id);
    if (gfx_fd < 0)
        return -1;
    put_u32be(idbuf, window_id);
    if (cwm_send_fd(client_socket, CWM_PRT_TYPE_CREATE_WINDOW_REPLY, idbuf, 4, gfx_fd) < 0) {
        window_destroy((int)window_id);
        return -1;
    }
    return 0;
}

int process_destroy_window(int client_socket)
{
    uint32_t window_id;

    if (server.recv_len != 5)
        return -1;
    window_id = uint32_from_buffer(server.recv_buffer + 1);
    return window_destroy_for_client(client_socket, window_id);
}

int process_commit_window(int client_socket)
{
    if (server.recv_len != 5)
        return -1;
    return window_owned_by_client(client_socket,
                                  uint32_from_buffer(server.recv_buffer + 1)) ? 0 : -1;
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
