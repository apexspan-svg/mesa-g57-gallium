/* Surfaceless EGL smoke test for Panfrost Gallium on kbase (/dev/mali0).
 *
 * Loads libEGL_mesa.so explicitly (so the kbase build is used even when the
 * system has its own EGL), brings up a surfaceless display against
 * MESA_KBASE_NODE, clears a pbuffer red and reads back the center pixel.
 * A red pixel proves: EGL init -> loader kbase hook -> kbase device open ->
 * JM job submit (clear = vtc+frag chains) -> completion.
 *
 * Build (Termux):  cc egl_kbase_triangle.c -ldl -o egl_kbase_triangle
 * Run:  MESA_KBASE_NODE=/dev/mali0 EGL_LIB=./libEGL_mesa.so ./egl_kbase_triangle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

int
main(void)
{
   const char *egl_lib = getenv("EGL_LIB");
   if (!egl_lib)
      egl_lib = "libEGL_mesa.so";

   void *egl = dlopen(egl_lib, RTLD_NOW | RTLD_LOCAL);
   if (!egl) {
      printf("FAIL: dlopen %s: %s\n", egl_lib, dlerror());
      return 1;
   }

   PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)dlsym(egl, "eglGetPlatformDisplayEXT");
   EGLDisplay (*eglGetDisplay)(EGLNativeDisplayType) =
      (EGLDisplay(*)(EGLNativeDisplayType))dlsym(egl, "eglGetDisplay");
   EGLBoolean (*eglInitialize)(EGLDisplay, EGLint *, EGLint *) =
      (EGLBoolean(*)(EGLDisplay, EGLint *, EGLint *))dlsym(egl, "eglInitialize");
   const char *(*eglQueryString)(EGLDisplay, EGLint) =
      (const char *(*)(EGLDisplay, EGLint))dlsym(egl, "eglQueryString");
   EGLBoolean (*eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) =
      (EGLBoolean(*)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *))dlsym(egl, "eglChooseConfig");
   EGLSurface (*eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint *) =
      (EGLSurface(*)(EGLDisplay, EGLConfig, const EGLint *))dlsym(egl, "eglCreatePbufferSurface");
   EGLContext (*eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) =
      (EGLContext(*)(EGLDisplay, EGLConfig, EGLContext, const EGLint *))dlsym(egl, "eglCreateContext");
   EGLBoolean (*eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) =
      (EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext))dlsym(egl, "eglMakeCurrent");
   __eglMustCastToProperFunctionPointerType (*eglGetProcAddress)(const char *) =
      (__eglMustCastToProperFunctionPointerType(*)(const char *))dlsym(egl, "eglGetProcAddress");
   EGLint (*eglGetError)(void) =
      (EGLint(*)(void))dlsym(egl, "eglGetError");

   if (!eglGetDisplay || !eglInitialize || !eglChooseConfig ||
       !eglCreatePbufferSurface || !eglCreateContext || !eglMakeCurrent ||
       !eglGetProcAddress) {
      printf("FAIL: missing EGL entry points\n");
      return 1;
   }

   EGLDisplay dpy = EGL_NO_DISPLAY;
   if (eglGetPlatformDisplayEXT)
      dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA,
                                     EGL_DEFAULT_DISPLAY, NULL);
   if (dpy == EGL_NO_DISPLAY && eglGetDisplay)
      dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (dpy == EGL_NO_DISPLAY) {
      printf("FAIL: no EGL display (err 0x%x)\n", eglGetError ? eglGetError() : -1);
      return 1;
   }

   if (!eglInitialize(dpy, NULL, NULL)) {
      printf("FAIL: eglInitialize (err 0x%x)\n", eglGetError());
      return 1;
   }
   printf("EGL version: %s\n", eglQueryString(dpy, EGL_VERSION));
   printf("EGL vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));

   static const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
      EGL_NONE,
   };
   EGLConfig cfg;
   EGLint ncfg = 0;
   if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
      printf("FAIL: eglChooseConfig (err 0x%x)\n", eglGetError());
      return 1;
   }

   static const EGLint pb_attr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
   EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb_attr);
   if (surf == EGL_NO_SURFACE) {
      printf("FAIL: pbuffer (err 0x%x)\n", eglGetError());
      return 1;
   }

   static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
   EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
   if (ctx == EGL_NO_CONTEXT) {
      printf("FAIL: context (err 0x%x)\n", eglGetError());
      return 1;
   }

   if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
      printf("FAIL: makeCurrent (err 0x%x)\n", eglGetError());
      return 1;
   }

   void (*glClearColor)(float, float, float, float) =
      (void(*)(float, float, float, float))eglGetProcAddress("glClearColor");
   void (*glClear)(unsigned) =
      (void(*)(unsigned))eglGetProcAddress("glClear");
   void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *) =
      (void(*)(int, int, int, int, unsigned, unsigned, void *))eglGetProcAddress("glReadPixels");
   const unsigned char *(*glGetString)(unsigned) =
      (const unsigned char *(*)(unsigned))eglGetProcAddress("glGetString");
   if (!glClearColor || !glClear || !glReadPixels || !glGetString) {
      printf("FAIL: missing GL entry points\n");
      return 1;
   }
   printf("GL vendor: %s\n", glGetString(0x1F00 /* GL_VENDOR */));
   printf("GL renderer: %s\n", glGetString(0x1F01 /* GL_RENDERER */));

   glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
   glClear(0x4000 /* GL_COLOR_BUFFER_BIT */);

   unsigned char px[4] = {0};
   glReadPixels(32, 32, 1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px);
   printf("center pixel: R=%u G=%u B=%u A=%u\n", px[0], px[1], px[2], px[3]);

   if (px[0] > 200 && px[1] < 50 && px[2] < 50) {
      printf("PASS: red clear rendered on kbase\n");
      return 0;
   }
   printf("FAIL: expected red\n");
   return 1;
}
