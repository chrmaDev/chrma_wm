#include "cwm_client.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static struct {
    int sock;
    int buf_fd;
    void *pixels;
    size_t map_size;
    int width;
    int height;
    uint32_t window_id;
    char clipboard[CWM_CLIPBOARD_MAX];
} g = {
    .sock = -1,
    .buf_fd = -1,
};

static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int send_bytes(const void *buf, size_t n)
{
    if (g.sock < 0)
        return -1;
    if (send(g.sock, buf, n, 0) < 0) {
        perror("send");
        return -1;
    }
    return 0;
}

static int recv_record(uint8_t *type, void *payload, uint32_t cap, uint32_t *length, int *out_fd)
{
    uint8_t buf[2048];
    struct msghdr msg;
    struct iovec iov;
    union {
        struct cmsghdr align;
        char cmsgbuf[CMSG_SPACE(sizeof(int))];
    } u;
    struct cmsghdr *cmsg;
    ssize_t n;
    uint32_t len;

    for (;;) {
        memset(&msg, 0, sizeof(msg));
        memset(&u, 0, sizeof(u));
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        if (out_fd) {
            msg.msg_control = u.cmsgbuf;
            msg.msg_controllen = sizeof(u.cmsgbuf);
        }

        n = recvmsg(g.sock, &msg, 0);
        if (n < CWM_HDR_SIZE) {
            perror("recvmsg");
            return -1;
        }
        *type = buf[0];
        len = get_u32be(buf + 1);
        if ((size_t)n != CWM_HDR_SIZE + (size_t)len)
            return -1;
        if (*type != CWM_PRT_TYPE_CLIPBOARD_UPDATE)
            break;
        if (len < 1 || len > CWM_CLIPBOARD_MAX ||
            buf[CWM_HDR_SIZE + len - 1] != '\0')
            return -1;
        memcpy(g.clipboard, buf + CWM_HDR_SIZE, len);
    }
    if (len > cap) return -1;
    if (length)
        *length = len;
    if (len && payload)
        memcpy(payload, buf + CWM_HDR_SIZE, len);

    if (out_fd) {
        int fd = -1;

        cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
            return -1;
        memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
        *out_fd = fd;
    }
    return 0;
}

static void unmap_pixels(void)
{
    if (g.pixels && g.pixels != MAP_FAILED)
        munmap(g.pixels, g.map_size);
    if (g.buf_fd >= 0)
        close(g.buf_fd);
    g.pixels = NULL;
    g.buf_fd = -1;
    g.map_size = 0;
    g.width = 0;
    g.height = 0;
}

static void drop_buffer(void)
{
    unmap_pixels();
    g.window_id = 0;
}

static int adopt_buffer(int fd, int width, int height)
{
    size_t size;
    void *map;

    if (fd < 0 || width <= 0 || height <= 0)
        return -1;
    size = (size_t)width * (size_t)height * 4;
    map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    unmap_pixels();
    g.buf_fd = fd;
    g.width = width;
    g.height = height;
    g.map_size = size;
    g.pixels = map;
    return 0;
}

int cwm_client_connect(void)
{
    struct sockaddr_un addr;

    if (g.sock >= 0)
        cwm_client_disconnect();
    g.clipboard[0] = '\0';

    g.sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (g.sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CWM_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(g.sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(g.sock);
        g.sock = -1;
        return -1;
    }
    return 0;
}

int cwm_client_disconnect(void)
{
    drop_buffer();
    if (g.sock >= 0)
        close(g.sock);
    g.sock = -1;
    return 0;
}

int cwm_request_create_window(const char *title, int width, int height)
{
    uint8_t msg[17 + 256 + 4];
    size_t title_len;
    size_t n;

    if (g.sock < 0 || width <= 0 || height <= 0)
        return -1;

    drop_buffer();
    memset(msg, 0, sizeof(msg));
    msg[0] = CWM_PRT_TYPE_CREATE_WINDOW;
    put_u32be(msg + 1, 40);
    put_u32be(msg + 5, 40);
    put_u32be(msg + 9, (uint32_t)width);
    put_u32be(msg + 13, (uint32_t)height);

    title_len = title ? strlen(title) : 0;
    if (title_len > 255)
        title_len = 255;
    if (title_len)
        memcpy(msg + 17, title, title_len);
    msg[17 + title_len] = '\0';
    put_u32be(msg + 17 + title_len + 1, CWM_RENDER_NATIVE);
    n = 17 + title_len + 1 + 4;

    if (send_bytes(msg, n) < 0)
        return -1;

    {
        uint8_t type;
        uint8_t idbuf[4];
        uint32_t len;

        if (recv_record(&type, idbuf, sizeof(idbuf), &len, &g.buf_fd) < 0)
            return -1;
        if (type != CWM_PRT_TYPE_CREATE_WINDOW_REPLY || len != 4)
            return -1;
        g.window_id = get_u32be(idbuf);
        if (g.window_id == 0)
            return -1;
    }

    g.width = width;
    g.height = height;
    g.map_size = (size_t)width * (size_t)height * 4;
    g.pixels = mmap(NULL, g.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, g.buf_fd, 0);
    if (g.pixels == MAP_FAILED) {
        perror("mmap");
        g.pixels = NULL;
        close(g.buf_fd);
        g.buf_fd = -1;
        g.window_id = 0;
        return -1;
    }
    return (int)g.window_id;
}

int cwm_request_destroy_window(int window_id)
{
    uint8_t msg[5];

    if (window_id <= 0)
        return -1;
    msg[0] = CWM_PRT_TYPE_DESTROY_WINDOW;
    put_u32be(msg + 1, (uint32_t)window_id);
    if (send_bytes(msg, sizeof(msg)) < 0)
        return -1;
    drop_buffer();
    return 0;
}

int cwm_request_commit_window(int window_id)
{
    uint8_t msg[5];

    if (window_id <= 0)
        return -1;
    msg[0] = CWM_PRT_TYPE_COMMIT_WINDOW;
    put_u32be(msg + 1, (uint32_t)window_id);
    /* Pixels are already in the shared mapping. This packet only marks the
     * frame coherent so the compositor can blit on the next dirty pass. */
    return send_bytes(msg, sizeof(msg));
}

int cwm_request_set_title(int window_id, const char *title)
{
    uint8_t msg[5 + 256];
    size_t len;

    if (window_id <= 0 || !title) return -1;
    len = strlen(title);
    if (len > 255) return -1;
    msg[0] = CWM_PRT_TYPE_SET_WINDOW_TITLE;
    put_u32be(msg + 1, (uint32_t)window_id);
    memcpy(msg + 5, title, len + 1);
    return send_bytes(msg, 5 + len + 1);
}

int cwm_request_set_size(int window_id, int width, int height)
{
    uint8_t msg[13];

    if (window_id <= 0 || width <= 0 || height <= 0) return -1;
    msg[0] = CWM_PRT_TYPE_SET_WINDOW_SIZE;
    put_u32be(msg + 1, (uint32_t)window_id);
    put_u32be(msg + 5, (uint32_t)width);
    put_u32be(msg + 9, (uint32_t)height);
    return send_bytes(msg, sizeof(msg));
}

int cwm_request_set_state(int window_id, enum cwmWindowState state)
{
    uint8_t msg[6];

    if (window_id <= 0 || state > CWM_WINDOW_FULLSCREEN) return -1;
    msg[0] = CWM_PRT_TYPE_SET_WINDOW_STATE;
    put_u32be(msg + 1, (uint32_t)window_id);
    msg[5] = (uint8_t)state;
    return send_bytes(msg, sizeof(msg));
}

int cwm_client_set_clipboard(const char *text)
{
    uint8_t msg[1 + CWM_CLIPBOARD_MAX];
    size_t len;

    if (!text) return -1;
    len = strlen(text);
    if (len >= CWM_CLIPBOARD_MAX) return -1;
    msg[0] = CWM_PRT_TYPE_SET_CLIPBOARD;
    memcpy(msg + 1, text, len + 1);
    if (send_bytes(msg, len + 2) < 0) return -1;
    memcpy(g.clipboard, text, len + 1);
    return 0;
}

const char *cwm_client_get_clipboard(void)
{
    return g.clipboard;
}

int cwm_request_get_info(struct get_info_response *out)
{
    uint8_t type = CWM_PRT_TYPE_GET_INFO;
    uint8_t reply_type;
    uint32_t len;

    if (!out || send_bytes(&type, 1) < 0)
        return -1;
    if (recv_record(&reply_type, out, sizeof(*out), &len, NULL) < 0)
        return -1;
    if (reply_type != CWM_PRT_TYPE_GET_INFO || len != sizeof(*out))
        return -1;
    return 0;
}

void *cwm_client_pixels(void)
{
    return g.pixels;
}

int cwm_client_width(void)
{
    return g.width;
}

int cwm_client_height(void)
{
    return g.height;
}

int cwm_client_stride(void)
{
    return g.width * 4;
}

void cwm_client_pixmap(pixmap *out)
{
    if (!out)
        return;
    out->data = g.pixels;
    out->width = (uint32_t)g.width;
    out->height = (uint32_t)g.height;
    out->pitch = (uint32_t)g.width * 4;
    out->pixel_stride = 4;
    out->bgr = 1;
}

int cwm_client_fd(void)
{
    return g.sock;
}

int cwm_client_poll_event(struct input_event *out)
{
    uint8_t buf[2048];
    struct msghdr msg;
    struct iovec iov;
    union {
        struct cmsghdr align;
        char cmsgbuf[CMSG_SPACE(sizeof(int))];
    } u;
    struct cmsghdr *cmsg;
    ssize_t n;
    uint32_t len;
    uint8_t type;

    if (!out || g.sock < 0)
        return -1;

    for (;;) {
        memset(&msg, 0, sizeof(msg));
        memset(&u, 0, sizeof(u));
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = u.cmsgbuf;
        msg.msg_controllen = sizeof(u.cmsgbuf);
        n = recvmsg(g.sock, &msg, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            perror("recvmsg");
            return -1;
        }
        if (n == 0)
            return -1;
        if (n < CWM_HDR_SIZE)
            return -1;
        type = buf[0];
        len = get_u32be(buf + 1);
        if ((size_t)n != CWM_HDR_SIZE + (size_t)len)
            return -1;

        if (type == CWM_PRT_TYPE_INPUT_EVENT) {
            if (len != sizeof(*out))
                return -1;
            memcpy(out, buf + CWM_HDR_SIZE, sizeof(*out));
            return 1;
        }
        if (type == CWM_PRT_TYPE_CLIPBOARD_UPDATE) {
            if (len < 1 || len > CWM_CLIPBOARD_MAX ||
                buf[CWM_HDR_SIZE + len - 1] != '\0')
                return -1;
            memcpy(g.clipboard, buf + CWM_HDR_SIZE, len);
            continue;
        }
        if (type == CWM_PRT_TYPE_CLOSE_WINDOW) {
            memset(out, 0, sizeof(*out));
            out->type = CWM_PRT_TYPE_CLOSE_WINDOW;
            return 1;
        }
        if (type == CWM_PRT_TYPE_RESIZE_WINDOW) {
            int fd = -1;
            uint32_t width, height;

            if (len != CWM_RESIZE_PAYLOAD)
                return -1;
            cmsg = CMSG_FIRSTHDR(&msg);
            if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
                return -1;
            memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
            width = get_u32be(buf + CWM_HDR_SIZE + 4);
            height = get_u32be(buf + CWM_HDR_SIZE + 8);
            if (adopt_buffer(fd, (int)width, (int)height) < 0)
                return -1;
            memset(out, 0, sizeof(*out));
            out->type = CWM_INPUT_EVENT_TYPE_RESIZE;
            out->x = (uint16_t)width;
            out->y = (uint16_t)height;
            return 1;
        }
    }
}
