#ifndef GSR_EGL_H
#define GSR_EGL_H

/* OpenGL EGL library with a hidden window context (to allow using the opengl functions) */

#include <X11/X.h>
#include <X11/Xutil.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x, y;
} vec2i;

#ifdef _WIN64
typedef signed   long long int khronos_intptr_t;
typedef unsigned long long int khronos_uintptr_t;
typedef signed   long long int khronos_ssize_t;
typedef unsigned long long int khronos_usize_t;
#else
typedef signed   long  int     khronos_intptr_t;
typedef unsigned long  int     khronos_uintptr_t;
typedef signed   long  int     khronos_ssize_t;
typedef unsigned long  int     khronos_usize_t;
#endif

typedef void* EGLDisplay;
typedef void* EGLNativeDisplayType;
typedef uintptr_t EGLNativeWindowType;
typedef uintptr_t EGLNativePixmapType;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLClientBuffer;
typedef void* EGLImage;
typedef void* EGLImageKHR;
typedef void *GLeglImageOES;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);

typedef int (*FUNC_eglQueryDisplayAttribEXT)(EGLDisplay dpy, int32_t attribute, intptr_t *value);
typedef const char* (*FUNC_eglQueryDeviceStringEXT)(void *device, int32_t name);

#define EGL_BUFFER_SIZE                         0x3020
#define EGL_RENDERABLE_TYPE                     0x3040
#define EGL_OPENGL_BIT                          0x0008
#define EGL_OPENGL_API                          0x30A2
#define EGL_NONE                                0x3038
#define EGL_CONTEXT_CLIENT_VERSION              0x3098
#define EGL_DEVICE_EXT                          0x322C
#define EGL_DRM_DEVICE_FILE_EXT                 0x3233

#define GL_VENDOR                               0x1F00
#define GL_RENDERER                             0x1F01

#define GSR_MAX_OUTPUTS 32

typedef struct {
    Display *dpy;
    Window window;
} gsr_x11;

typedef struct {
    uint32_t wl_name;
    void *output;
    vec2i pos;
    vec2i size;
    char *name;
} gsr_wayland_output;

typedef struct {
    void *dpy;
    void *window;
    void *registry;
    void *surface;
    void *compositor;
    gsr_wayland_output outputs[GSR_MAX_OUTPUTS];
    int num_outputs;
} gsr_wayland;

typedef struct {
    void *egl_library;
    void *gl_library;

    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    const char *dri_card_path;

    gsr_x11 x11;
    gsr_wayland wayland;
    char card_path[128];

    EGLDisplay (*eglGetDisplay)(EGLNativeDisplayType display_id);
    unsigned int (*eglInitialize)(EGLDisplay dpy, int32_t *major, int32_t *minor);
    unsigned int (*eglTerminate)(EGLDisplay dpy);
    unsigned int (*eglChooseConfig)(EGLDisplay dpy, const int32_t *attrib_list, EGLConfig *configs, int32_t config_size, int32_t *num_config);
    EGLSurface (*eglCreateWindowSurface)(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const int32_t *attrib_list);
    EGLContext (*eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const int32_t *attrib_list);
    unsigned int (*eglMakeCurrent)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
    unsigned int (*eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
    unsigned int (*eglDestroySurface)(EGLDisplay dpy, EGLSurface surface);
    unsigned int (*eglBindAPI)(unsigned int api);
    __eglMustCastToProperFunctionPointerType (*eglGetProcAddress)(const char *procname);

    FUNC_eglQueryDisplayAttribEXT eglQueryDisplayAttribEXT;
    FUNC_eglQueryDeviceStringEXT eglQueryDeviceStringEXT;

    const unsigned char* (*glGetString)(unsigned int name);
} gsr_egl;

bool gsr_egl_load(gsr_egl *self, Display *dpy, bool wayland);
void gsr_egl_unload(gsr_egl *self);

#endif /* GSR_EGL_H */
