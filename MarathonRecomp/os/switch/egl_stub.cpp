// No-op EGL entry points for the symbols SDL2's video core references.
//
// The devkitPro libEGL.a is built from an old Mesa and carries its own copies
// of Mesa utility code (ralloc, hash_table). Linking it next to the NVK
// Vulkan driver (a modern Mesa) makes the linker mix allocator ABIs across
// Mesa generations, corrupting the driver. SDL's GL path is never used on
// Switch (rendering goes through NVK Vulkan), so these stubs satisfy the
// linker instead and always fail cleanly.
#if defined(__SWITCH__)

#include <cstdint>

extern "C"
{
    typedef void* EGLDisplay;
    typedef void* EGLConfig;
    typedef void* EGLContext;
    typedef void* EGLSurface;
    typedef void* EGLNativeDisplayType;
    typedef void* EGLNativeWindowType;
    typedef unsigned int EGLBoolean;
    typedef unsigned int EGLenum;
    typedef int32_t EGLint;
    typedef void (*__eglMustCastToProperFunctionPointerType)(void);

    static constexpr EGLBoolean EGL_STUB_FALSE = 0;
    static constexpr EGLint EGL_STUB_NOT_INITIALIZED = 0x3001;

    EGLint eglGetError(void) { return EGL_STUB_NOT_INITIALIZED; }
    EGLDisplay eglGetDisplay(EGLNativeDisplayType) { return nullptr; }
    EGLDisplay eglGetPlatformDisplay(EGLenum, void*, const intptr_t*) { return nullptr; }
    EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*) { return EGL_STUB_FALSE; }
    EGLBoolean eglTerminate(EGLDisplay) { return EGL_STUB_FALSE; }
    const char* eglQueryString(EGLDisplay, EGLint) { return nullptr; }
    EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) { return EGL_STUB_FALSE; }
    EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint, EGLint*) { return EGL_STUB_FALSE; }
    EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*) { return nullptr; }
    EGLBoolean eglDestroyContext(EGLDisplay, EGLContext) { return EGL_STUB_FALSE; }
    EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) { return nullptr; }
    EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const EGLint*) { return nullptr; }
    EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface) { return EGL_STUB_FALSE; }
    EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext) { return EGL_STUB_FALSE; }
    EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface) { return EGL_STUB_FALSE; }
    EGLBoolean eglSwapInterval(EGLDisplay, EGLint) { return EGL_STUB_FALSE; }
    EGLBoolean eglBindAPI(EGLenum) { return EGL_STUB_FALSE; }
    EGLenum eglQueryAPI(void) { return 0; }
    EGLBoolean eglWaitGL(void) { return EGL_STUB_FALSE; }
    EGLBoolean eglWaitNative(EGLint) { return EGL_STUB_FALSE; }
    __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char*) { return nullptr; }
}

#endif
