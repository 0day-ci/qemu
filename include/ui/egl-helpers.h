#ifndef EGL_HELPERS_H
#define EGL_HELPERS_H

#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <gbm.h>

extern EGLDisplay *qemu_egl_display;
extern EGLConfig qemu_egl_config;

typedef struct egl_fb {
    int width;
    int height;
    GLuint texture;
    GLuint framebuffer;
    bool delete_texture;
} egl_fb;

void egl_fb_destroy(egl_fb *fb);
void egl_fb_create_for_tex(egl_fb *fb, int width, int height, GLuint texture);
void egl_fb_create_new_tex(egl_fb *fb, int width, int height);
void egl_fb_blit(egl_fb *dst, egl_fb *src, bool flip);
void egl_fb_read(void *dst, egl_fb *src);

#ifdef CONFIG_OPENGL_DMABUF

extern int qemu_egl_rn_fd;
extern struct gbm_device *qemu_egl_rn_gbm_dev;
extern EGLContext qemu_egl_rn_ctx;

int egl_rendernode_init(const char *rendernode);
int egl_get_fd_for_texture(uint32_t tex_id, EGLint *stride, EGLint *fourcc);

#endif

EGLSurface qemu_egl_init_surface_x11(EGLContext ectx, Window win);

int qemu_egl_init_dpy_x11(EGLNativeDisplayType dpy);
int qemu_egl_init_dpy_mesa(EGLNativeDisplayType dpy);
EGLContext qemu_egl_init_ctx(void);

#endif /* EGL_HELPERS_H */
