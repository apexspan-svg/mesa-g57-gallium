/* GLES2 shaded-triangle test for Panfrost Gallium on kbase.
 * Beyond the clear test: vertex fetch + VS compile + IDVS (MALLOC_VERTEX on
 * v9) + varying + FS compile + tiler + fragment. Blue clear, red triangle
 * covering the center; corner must stay blue.
 *
 * Build (Termux):  cc egl_kbase_gles_triangle.c -ldl -o egl_kbase_gles_triangle
 * Run:  MESA_KBASE_NODE=/dev/mali0 EGL_LIB=./libEGL_mesa.so ./egl_kbase_gles_triangle
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

#define GLfn(ret, name, ...) \
   ret (*name)(__VA_ARGS__) = \
      (ret(*)(__VA_ARGS__))eglGetProcAddress("gl" #name)

int
main(void)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   const char *egl_lib = getenv("EGL_LIB");
   if (!egl_lib)
      egl_lib = "libEGL_mesa.so";

   void *egl = dlopen(egl_lib, RTLD_NOW | RTLD_LOCAL);
   if (!egl) {
      printf("FAIL: dlopen %s: %s\n", egl_lib, dlerror());
      return 1;
   }

   EGLDisplay (*eglGetPlatformDisplay)(EGLenum, void *, const EGLint *) =
      (EGLDisplay(*)(EGLenum, void *, const EGLint *))dlsym(egl, "eglGetPlatformDisplay");
   EGLBoolean (*eglInitialize)(EGLDisplay, EGLint *, EGLint *) =
      (EGLBoolean(*)(EGLDisplay, EGLint *, EGLint *))dlsym(egl, "eglInitialize");
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
   const char *(*eglQueryString)(EGLDisplay, EGLint) =
      (const char *(*)(EGLDisplay, EGLint))dlsym(egl, "eglQueryString");

   EGLDisplay dpy = EGL_NO_DISPLAY;
   if (eglGetPlatformDisplay)
      dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
      printf("FAIL: display/init (err 0x%x)\n", eglGetError());
      return 1;
   }
   printf("GL renderer: %s\n", "((query after context))");

   static const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
   };
   EGLConfig cfg;
   EGLint ncfg = 0;
   if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
      printf("FAIL: config (err 0x%x)\n", eglGetError());
      return 1;
   }
   static const EGLint pb_attr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
   EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb_attr);
   static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
   EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
   if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT ||
       !eglMakeCurrent(dpy, surf, surf, ctx)) {
      printf("FAIL: surface/context (err 0x%x)\n", eglGetError());
      return 1;
   }

   /* GL entry points */
   void (*glViewport)(int, int, int, int) =
      (void(*)(int, int, int, int))eglGetProcAddress("glViewport");
   void (*glClearColor)(float, float, float, float) =
      (void(*)(float, float, float, float))eglGetProcAddress("glClearColor");
   void (*glClear)(unsigned) =
      (void(*)(unsigned))eglGetProcAddress("glClear");
   unsigned (*glCreateShader)(unsigned) =
      (unsigned(*)(unsigned))eglGetProcAddress("glCreateShader");
   void (*glShaderSource)(unsigned, int, const char *const *, const int *) =
      (void(*)(unsigned, int, const char *const *, const int *))eglGetProcAddress("glShaderSource");
   void (*glCompileShader)(unsigned) =
      (void(*)(unsigned))eglGetProcAddress("glCompileShader");
   void (*glGetShaderiv)(unsigned, unsigned, int *) =
      (void(*)(unsigned, unsigned, int *))eglGetProcAddress("glGetShaderiv");
   void (*glGetShaderInfoLog)(unsigned, int, int *, char *) =
      (void(*)(unsigned, int, int *, char *))eglGetProcAddress("glGetShaderInfoLog");
   unsigned (*glCreateProgram)(void) =
      (unsigned(*)(void))eglGetProcAddress("glCreateProgram");
   void (*glAttachShader)(unsigned, unsigned) =
      (void(*)(unsigned, unsigned))eglGetProcAddress("glAttachShader");
   void (*glBindAttribLocation)(unsigned, unsigned, const char *) =
      (void(*)(unsigned, unsigned, const char *))eglGetProcAddress("glBindAttribLocation");
   void (*glLinkProgram)(unsigned) =
      (void(*)(unsigned))eglGetProcAddress("glLinkProgram");
   void (*glGetProgramiv)(unsigned, unsigned, int *) =
      (void(*)(unsigned, unsigned, int *))eglGetProcAddress("glGetProgramiv");
   void (*glUseProgram)(unsigned) =
      (void(*)(unsigned))eglGetProcAddress("glUseProgram");
   void (*glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *) =
      (void(*)(unsigned, int, unsigned, unsigned char, int, const void *))eglGetProcAddress("glVertexAttribPointer");
   void (*glEnableVertexAttribArray)(unsigned) =
      (void(*)(unsigned))eglGetProcAddress("glEnableVertexAttribArray");
   void (*glDrawArrays)(unsigned, int, int) =
      (void(*)(unsigned, int, int))eglGetProcAddress("glDrawArrays");
   void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *) =
      (void(*)(int, int, int, int, unsigned, unsigned, void *))eglGetProcAddress("glReadPixels");
   const unsigned char *(*glGetString)(unsigned) =
      (const unsigned char *(*)(unsigned))eglGetProcAddress("glGetString");
   unsigned (*glGetError)(void) =
      (unsigned(*)(void))eglGetProcAddress("glGetError");

   printf("GL renderer: %s\n", glGetString(0x1F01));

   static const char vs_src[] =
      "attribute vec4 a_pos;\n"
      "void main() { gl_Position = a_pos; }\n";
   static const char fs_src[] =
      "precision mediump float;\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

   printf("creating VS...\n");
   unsigned vs = glCreateShader(0x8B31 /* VERTEX_SHADER */);
   printf("vs=%u\n", vs);
   { const char *p = vs_src;
   glShaderSource(vs, 1, &p, NULL); }
   printf("compiling VS...\n");
   glCompileShader(vs);
   printf("VS compiled\n");
   int ok = 0;
   glGetShaderiv(vs, 0x8B81 /* COMPILE_STATUS */, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetShaderInfoLog(vs, sizeof(log) - 1, NULL, log);
      printf("FAIL: VS compile: %s\n", log);
      return 1;
   }
   unsigned fs = glCreateShader(0x8B30 /* FRAGMENT_SHADER */);
   { const char *p = fs_src;
   glShaderSource(fs, 1, &p, NULL); }
   glCompileShader(fs);
   glGetShaderiv(fs, 0x8B81, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetShaderInfoLog(fs, sizeof(log) - 1, NULL, log);
      printf("FAIL: FS compile: %s\n", log);
      return 1;
   }
   printf("shaders compiled\n");

   unsigned prog = glCreateProgram();
   glAttachShader(prog, vs);
   glAttachShader(prog, fs);
   glBindAttribLocation(prog, 0, "a_pos");
   glLinkProgram(prog);
   glGetProgramiv(prog, 0x8B82 /* LINK_STATUS */, &ok);
   if (!ok) {
      printf("FAIL: link\n");
      return 1;
   }
   glUseProgram(prog);

   glViewport(0, 0, 64, 64);
   glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
   glClear(0x4000);

   static const float verts[] = {
      -0.8f, -0.8f, 0.0f,
       0.8f, -0.8f, 0.0f,
       0.0f,  0.8f, 0.0f,
   };
   glVertexAttribPointer(0, 3, 0x1406 /* FLOAT */, 0, 0, verts);
   glEnableVertexAttribArray(0);
   glDrawArrays(0x0004 /* TRIANGLES */, 0, 3);

   unsigned err = glGetError();
   printf("draw glError: 0x%x\n", err);

   unsigned char c[4] = {0}, corner[4] = {0};
   glReadPixels(32, 32, 1, 1, 0x1908, 0x1401, c);
   glReadPixels(4, 4, 1, 1, 0x1908, 0x1401, corner);
   printf("center: R=%u G=%u B=%u | corner: R=%u G=%u B=%u\n",
          c[0], c[1], c[2], corner[0], corner[1], corner[2]);

   if (c[0] > 200 && c[1] < 50 && c[2] < 50 &&
       corner[2] > 200 && corner[0] < 50) {
      printf("PASS: shaded triangle on kbase\n");
      return 0;
   }
   printf("FAIL: wrong pixels\n");
   return 1;
}
