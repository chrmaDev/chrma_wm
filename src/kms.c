#include "kms.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define DEFAULT_RENDER_SCALE 4

typedef struct {
    pixmap px;
    uint32_t handle;
    uint32_t fb_id;
    uint64_t map_size;
} kms_fb;

static struct {
    int fd;
    int tty_fd;
    int orig_kd_mode;
    int orig_kb_mode;
    int kb_mode_saved;
    drmModeRes *res;
    drmModeConnector *connector;
    drmModeModeInfo mode;
    drmModeCrtc *crtc;
    uint32_t render_scale;
    kms_fb fb[2];
    int front; /* index currently on the CRTC */
    int queued; /* index waiting for vblank, valid if flip_pending */
    int flip_pending;
    int nfb;
} g;

int get_width(void) {
    return (int)g.mode.hdisplay;
}

int get_height(void) {
    return (int)g.mode.vdisplay;
}

int get_render_scale(void) {
    return g.render_scale > 0 ? (int)g.render_scale : 1;
}

int get_refresh_rate(void) {
    return (int)g.mode.vrefresh;
}

static void on_page_flip(int fd, unsigned int sequence, unsigned int tv_sec,
                         unsigned int tv_usec, void *user_data)
{
    (void)fd;
    (void)sequence;
    (void)tv_sec;
    (void)tv_usec;
    (void)user_data;
    g.front = g.queued;
    g.flip_pending = 0;
}

static void drm_events(void)
{
    drmEventContext ev;

    memset(&ev, 0, sizeof(ev));
    /* Version 2: page_flip_handler only. Higher versions call NULL handler2. */
    ev.version = 2;
    ev.page_flip_handler = on_page_flip;
    drmHandleEvent(g.fd, &ev);
}

static void wait_flip(void)
{
    struct pollfd pfd;

    pfd.fd = g.fd;
    pfd.events = POLLIN;
    while (g.flip_pending) {
        if (poll(&pfd, 1, 100) <= 0)
            break;
        drm_events();
    }
}

static void destroy_fb(kms_fb *fb)
{
    struct drm_mode_destroy_dumb destroy;

    if (fb->fb_id)
        drmModeRmFB(g.fd, fb->fb_id);
    if (fb->px.data && fb->px.data != MAP_FAILED)
        munmap(fb->px.data, (size_t)fb->map_size);
    if (fb->handle) {
        memset(&destroy, 0, sizeof(destroy));
        destroy.handle = fb->handle;
        ioctl(g.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    memset(fb, 0, sizeof(*fb));
}

static int create_fb(kms_fb *fb, uint32_t width, uint32_t height)
{
    struct drm_mode_create_dumb create;
    struct drm_mode_map_dumb map;
    uint32_t fb_id;
    void *ptr;

    memset(&create, 0, sizeof(create));
    create.width = width;
    create.height = height;
    create.bpp = 32;

    if (ioctl(g.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("CREATE_DUMB");
        return -1;
    }
    fb->handle = create.handle;
    fb->map_size = create.size;

    memset(&map, 0, sizeof(map));
    map.handle = create.handle;
    if (ioctl(g.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        perror("MAP_DUMB");
        return -1;
    }

    ptr = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, g.fd, map.offset);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    if (drmModeAddFB(g.fd, create.width, create.height, 24, 32, create.pitch, create.handle, &fb_id)) {
        perror("AddFB");
        munmap(ptr, create.size);
        fb->px.data = NULL;
        return -1;
    }

    fb->fb_id = fb_id;
    fb->px.data = ptr;
    fb->px.width = create.width;
    fb->px.height = create.height;
    fb->px.pitch = create.pitch;
    fb->px.pixel_stride = create.bpp / 8;
    fb->px.bgr = 1;
    return 0;
}

static int pick_connector_crtc(void)
{
    int i, j;

    for (i = 0; i < g.res->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(g.fd, g.res->connectors[i]);

        if (!conn)
            continue;
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            g.connector = conn;
            break;
        }
        drmModeFreeConnector(conn);
    }
    if (!g.connector) {
        fprintf(stderr, "no connected DRM connector\n");
        return -1;
    }

    g.mode = g.connector->modes[0];
    for (i = 0; i < g.connector->count_modes; i++) {
        if (g.connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            g.mode = g.connector->modes[i];
            break;
        }
    }

    if (g.connector->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(g.fd, g.connector->encoder_id);

        if (enc) {
            if (enc->crtc_id)
                g.crtc = drmModeGetCrtc(g.fd, enc->crtc_id);
            drmModeFreeEncoder(enc);
        }
    }

    if (!g.crtc) {
        for (i = 0; i < g.connector->count_encoders; i++) {
            drmModeEncoder *enc = drmModeGetEncoder(g.fd, g.connector->encoders[i]);

            if (!enc)
                continue;
            for (j = 0; j < g.res->count_crtcs; j++) {
                if (enc->possible_crtcs & (1u << j)) {
                    g.crtc = drmModeGetCrtc(g.fd, g.res->crtcs[j]);
                    drmModeFreeEncoder(enc);
                    return g.crtc ? 0 : -1;
                }
            }
            drmModeFreeEncoder(enc);
        }
    }

    if (!g.crtc) {
        fprintf(stderr, "no CRTC for connector\n");
        return -1;
    }
    return 0;
}

int kms_init(void)
{
    int i;

    memset(&g, 0, sizeof(g));
    g.fd = -1;
    g.tty_fd = -1;
    g.orig_kd_mode = KD_TEXT;

    g.fd = open("/dev/dri/card0", O_RDWR);
    if (g.fd < 0) {
        perror("/dev/dri/card0");
        return -1;
    }

    g.res = drmModeGetResources(g.fd);
    if (!g.res) {
        fprintf(stderr, "drmModeGetResources failed\n");
        kms_cleanup();
        return -1;
    }

    if (pick_connector_crtc() < 0) {
        kms_cleanup();
        return -1;
    }

    for (i = 0; i < 2; i++) {
        if (create_fb(&g.fb[i], g.mode.hdisplay, g.mode.vdisplay) < 0) {
            kms_cleanup();
            return -1;
        }
        g.nfb++;
    }

    g.front = 0;
    if (drmModeSetCrtc(g.fd, g.crtc->crtc_id, g.fb[g.front].fb_id, 0, 0,
                       &g.connector->connector_id, 1, &g.mode)) {
        perror("SetCrtc");
        kms_cleanup();
        return -1;
    }

    g.tty_fd = open("/dev/tty", O_RDWR);
    if (g.tty_fd >= 0) {
        if (ioctl(g.tty_fd, KDGETMODE, &g.orig_kd_mode) < 0)
            g.orig_kd_mode = KD_TEXT;
        ioctl(g.tty_fd, KDSETMODE, KD_GRAPHICS);
        if (ioctl(g.tty_fd, KDGKBMODE, &g.orig_kb_mode) == 0) {
            g.kb_mode_saved = 1;
            ioctl(g.tty_fd, KDSKBMODE, K_OFF);
        }
    }
    g.render_scale = DEFAULT_RENDER_SCALE;
    return 0;
}

void kms_cleanup(void)
{
    int i;

    if (g.fd >= 0)
        wait_flip();

    if (g.fd >= 0 && g.crtc && g.connector) {
        drmModeSetCrtc(g.fd, g.crtc->crtc_id, g.crtc->buffer_id,
                       g.crtc->x, g.crtc->y, &g.connector->connector_id, 1,
                       g.crtc->mode_valid ? &g.crtc->mode : NULL);
    }

    if (g.tty_fd >= 0) {
        if (g.kb_mode_saved)
            ioctl(g.tty_fd, KDSKBMODE, g.orig_kb_mode);
        ioctl(g.tty_fd, KDSETMODE, g.orig_kd_mode);
        close(g.tty_fd);
        g.tty_fd = -1;
    }

    for (i = 0; i < g.nfb; i++)
        destroy_fb(&g.fb[i]);
    g.nfb = 0;

    if (g.crtc)
        drmModeFreeCrtc(g.crtc);
    if (g.connector)
        drmModeFreeConnector(g.connector);
    if (g.res)
        drmModeFreeResources(g.res);
    if (g.fd >= 0)
        close(g.fd);

    memset(&g, 0, sizeof(g));
    g.fd = -1;
    g.tty_fd = -1;
}

int kms_fd(void)
{
    return g.fd;
}

void kms_dispatch(void)
{
    if (g.fd >= 0)
        drm_events();
}

int kms_flip_pending(void)
{
    return g.flip_pending;
}

pixmap *kms_buffer(void)
{
    return &g.fb[g.front ^ 1].px;
}

int kms_present(void)
{
    int back = g.front ^ 1;

    if (g.flip_pending) {
        errno = EBUSY;
        perror("PageFlip");
        return -1;
    }
    if (drmModePageFlip(g.fd, g.crtc->crtc_id, g.fb[back].fb_id,
                        DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0) {
        perror("PageFlip");
        return -1;
    }
    g.queued = back;
    g.flip_pending = 1;
    return 0;

}