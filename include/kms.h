#ifndef KMS_H
#define KMS_H

#include "rendering.h"

int kms_init(void);
void kms_cleanup(void);

int get_refresh_rate(void);
int get_width(void);
int get_height(void);
int get_render_scale(void);

int kms_fd(void);
void kms_dispatch(void);
int kms_flip_pending(void);

/* Off-screen pixmap for this frame. Only valid when !kms_flip_pending().
 * Stable until kms_present(). */
pixmap *kms_buffer(void);

/* Queue the back buffer for scan-out at the next vblank. The previous
 * flip must have completed. Returns 0 on success. */
int kms_present(void);

#endif
