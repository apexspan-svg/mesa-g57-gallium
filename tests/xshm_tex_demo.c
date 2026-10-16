/* Textured-quad test for Panfrost Gallium on kbase.
 * 2x2 RGBA texture (R,G,B,W), NEAREST, fullscreen quad. Verifies texture
 * upload + sampler + varying interpolation on Valhall v9.
 *
 * Build: cc egl_kbase_textured.c -ldl -o egl_kbase_textured
 * Run: MESA_KBASE_NODE=/dev/mali0 EGL_LIB=./libEGL_mesa.so ./egl_kbase_textured
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/shm.h>

int
main(void)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   const char *egl_lib = getenv("EGL_LIB");
   if (!egl_lib)
      egl_lib = "libEGL_mesa.so";
   void *egl = dlopen(egl_lib, RTLD_NOW | RTLD_LOCAL);
   if (!egl) {
      printf("FAIL: dlopen\n");
      return 1;
   }
   EGLDisplay (*eglGetPlatformDisplay)(EGLenum, void *, const EGLint *) =
      (void*)dlsym(egl, "eglGetPlatformDisplay");
   EGLBoolean (*eglInitialize)(EGLDisplay, EGLint *, EGLint *) =
      (void*)dlsym(egl, "eglInitialize");
   EGLBoolean (*eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) =
      (void*)dlsym(egl, "eglChooseConfig");
   EGLSurface (*eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint *) =
      (void*)dlsym(egl, "eglCreatePbufferSurface");
   EGLContext (*eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) =
      (void*)dlsym(egl, "eglCreateContext");
   EGLBoolean (*eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) =
      (void*)dlsym(egl, "eglMakeCurrent");
   void *(*ga)(const char *) = (void*)dlsym(egl, "eglGetProcAddress");
   EGLint (*eglGetError)(void) = (void*)dlsym(egl, "eglGetError");

   EGLDisplay dpy = eglGetPlatformDisplay(0x31DD, EGL_DEFAULT_DISPLAY, NULL);
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
      printf("FAIL: init 0x%x\n", eglGetError());
      return 1;
   }
   static const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
   EGLConfig cfg;
   EGLint ncfg = 0;
   eglChooseConfig(dpy, ca, &cfg, 1, &ncfg);
   static const EGLint pa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
   EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pa);
   static const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
   EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
   if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
      printf("FAIL: current 0x%x\n", eglGetError());
      return 1;
   }

   void (*glViewport)(int, int, int, int) = (void*)ga("glViewport");
   void (*glClearColor)(float, float, float, float) = (void*)ga("glClearColor");
   void (*glClear)(unsigned) = (void*)ga("glClear");
   unsigned (*glCreateShader)(unsigned) = (void*)ga("glCreateShader");
   void (*glShaderSource)(unsigned, int, const char *const *, const int *) = (void*)ga("glShaderSource");
   void (*glCompileShader)(unsigned) = (void*)ga("glCompileShader");
   void (*glGetShaderiv)(unsigned, unsigned, int *) = (void*)ga("glGetShaderiv");
   void (*glGetShaderInfoLog)(unsigned, int, int *, char *) = (void*)ga("glGetShaderInfoLog");
   unsigned (*glCreateProgram)(void) = (void*)ga("glCreateProgram");
   void (*glAttachShader)(unsigned, unsigned) = (void*)ga("glAttachShader");
   void (*glBindAttribLocation)(unsigned, unsigned, const char *) = (void*)ga("glBindAttribLocation");
   void (*glLinkProgram)(unsigned) = (void*)ga("glLinkProgram");
   void (*glGetProgramiv)(unsigned, unsigned, int *) = (void*)ga("glGetProgramiv");
   void (*glUseProgram)(unsigned) = (void*)ga("glUseProgram");
   void (*glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *) = (void*)ga("glVertexAttribPointer");
   void (*glEnableVertexAttribArray)(unsigned) = (void*)ga("glEnableVertexAttribArray");
   void (*glDrawArrays)(unsigned, int, int) = (void*)ga("glDrawArrays");
   void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *) = (void*)ga("glReadPixels");
   void (*glGenTextures)(int, unsigned *) = (void*)ga("glGenTextures");
   void (*glBindTexture)(unsigned, unsigned) = (void*)ga("glBindTexture");
   void (*glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *) = (void*)ga("glTexImage2D");
   void (*glTexParameteri)(unsigned, unsigned, int) = (void*)ga("glTexParameteri");
   void (*glActiveTexture)(unsigned) = (void*)ga("glActiveTexture");
   int (*glGetUniformLocation)(unsigned, const char *) = (void*)ga("glGetUniformLocation");
   void (*glUniform1i)(int, int) = (void*)ga("glUniform1i");
   unsigned (*glGetError)(void) = (void*)ga("glGetError");

   static const char vs_src[] =
      "attribute vec4 a_pos;\nattribute vec2 a_uv;\n"
      "varying vec2 v_uv;\n"
      "void main(){gl_Position=a_pos;v_uv=a_uv;}\n";
   static const char fs_src[] =
      "precision mediump float;\n"
      "varying vec2 v_uv;\nuniform sampler2D u_tex;\n"
      "void main(){gl_FragColor=texture2D(u_tex,v_uv);}\n";
   const char *vs_p = vs_src, *fs_p = fs_src;
   unsigned vs = glCreateShader(0x8B31);
   glShaderSource(vs, 1, &vs_p, NULL);
   glCompileShader(vs);
   int ok = 0;
   glGetShaderiv(vs, 0x8B81, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetShaderInfoLog(vs, sizeof(log) - 1, NULL, log);
      printf("FAIL: VS: %s\n", log);
      return 1;
   }
   unsigned fs = glCreateShader(0x8B30);
   glShaderSource(fs, 1, &fs_p, NULL);
   glCompileShader(fs);
   glGetShaderiv(fs, 0x8B81, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetShaderInfoLog(fs, sizeof(log) - 1, NULL, log);
      printf("FAIL: FS: %s\n", log);
      return 1;
   }
   printf("shaders compiled\n");

   /* 2x2 RGBA: row0(bottom): red, green; row1(top): blue, white */
   static const unsigned char texels[2 * 2 * 4] = {
      255, 0, 0, 255, 0, 255, 0, 255,
      0, 0, 255, 255, 255, 255, 255, 255,
   };
   unsigned tex = 0;
   glGenTextures(1, &tex);
   glBindTexture(0x0DE1 /* TEXTURE_2D */, tex);
   glTexParameteri(0x0DE1, 0x2800 /* MAG_FILTER */, 0x2600 /* NEAREST */);
   glTexParameteri(0x0DE1, 0x2801 /* MIN_FILTER */, 0x2600);
   glTexParameteri(0x0DE1, 0x2802 /* WRAP_S */, 0x2901 /* REPEAT */);
   glTexParameteri(0x0DE1, 0x2803 /* WRAP_T */, 0x2901);
   glTexImage2D(0x0DE1, 0, 0x1908 /* RGBA */, 2, 2, 0, 0x1908, 0x1401, texels);
   printf("texture uploaded, err=0x%x\n", glGetError());

   unsigned prog = glCreateProgram();
   glAttachShader(prog, vs);
   glAttachShader(prog, fs);
   glBindAttribLocation(prog, 0, "a_pos");
   glBindAttribLocation(prog, 1, "a_uv");
   glLinkProgram(prog);
   glGetProgramiv(prog, 0x8B82, &ok);
   if (!ok) {
      printf("FAIL: link\n");
      return 1;
   }
   glUseProgram(prog);
   glActiveTexture(0x84C0 /* TEXTURE0 */);
   glBindTexture(0x0DE1, tex);
   glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);

   glViewport(0, 0, 64, 64);
   glClearColor(0, 0, 0, 1);
   glClear(0x4000);

   /* Fullscreen quad as triangle strip, uv 0..1 */
   static const float pos[] = { -1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0 };
   static const float uv[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
   glVertexAttribPointer(0, 3, 0x1406, 0, 0, pos);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 2, 0x1406, 0, 0, uv);
   glEnableVertexAttribArray(1);
   glDrawArrays(0x0005 /* TRIANGLE_STRIP */, 0, 4);
   printf("draw err=0x%x\n", glGetError());


   /* SHM present to X11 window */
   xcb_connection_t *conn = xcb_connect(NULL, NULL);
   xcb_screen_t *scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
   xcb_window_t win = xcb_generate_id(conn);
   uint32_t mask = XCB_CW_BACK_PIXEL;
   uint32_t vals[] = { scr->black_pixel };
   xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root,
                     10, 10, 64, 64, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     scr->root_visual, mask, vals);
   xcb_map_window(conn, win);
   xcb_flush(conn);
   sleep(1);
   xcb_gcontext_t gc = xcb_generate_id(conn);
   uint32_t gm = XCB_GC_FOREGROUND; uint32_t gv[] = { scr->white_pixel };
   xcb_create_gc(conn, gc, win, gm, gv);
   int shmid = shmget(IPC_PRIVATE, 64 * 64 * 4, IPC_CREAT | 0600);
   uint8_t *shm = shmat(shmid, NULL, 0);
   xcb_shm_seg_t seg = xcb_generate_id(conn);
   xcb_shm_attach(conn, seg, shmid, 0);
   shmctl(shmid, IPC_RMID, NULL);
   static unsigned char rgba[64 * 64 * 4];
   glReadPixels(0, 0, 64, 64, 0x1908, 0x1401, rgba);
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) {
         unsigned char *s = rgba + ((64 - 1 - y) * 64 + x) * 4;
         uint8_t *d = shm + (y * 64 + x) * 4;
         d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0;
      }
   printf("window 0x%x up 90s - look top-left\n", win);
   for (int i = 0; i < 22; i++) {
      xcb_shm_put_image(conn, win, gc, 64, 64, 0, 0, 64, 64, 0, 0,
                        scr->root_depth, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, seg, 0);
      xcb_flush(conn);
      sleep(4);
   }
   return 0;
}
