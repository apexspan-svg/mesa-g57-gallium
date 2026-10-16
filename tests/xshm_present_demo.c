/* GPU render -> X11 window via MIT-SHM (app-side present demo).
 * Renders the GLES2 triangle offscreen with Panfrost-on-kbase, then shows it
 * in an X11 window with xcb_shm_put_image. Proves the full display chain
 * with no driver present support needed.
 *
 * Build (Termux): cc xshm_demo.c -ldl -lxcb -lxcb-shm -o xshm_demo
 * Run: MESA_KBASE_NODE=/dev/mali0 EGL_LIB=./libEGL_mesa.so DISPLAY=:0 ./xshm_demo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <xcb/xcb.h>
#include <xcb/shm.h>

#define W 64
#define H 64

int
main(void)
{
   setvbuf(stdout, NULL, _IONBF, 0);

   /* ---- GLES render (same path as egl_kbase_gles_triangle) ---- */
   const char *egl_lib = getenv("EGL_LIB");
   if (!egl_lib)
      egl_lib = "libEGL_mesa.so";
   void *egl = dlopen(egl_lib, RTLD_NOW | RTLD_LOCAL);
   if (!egl) {
      printf("FAIL: dlopen: %s\n", dlerror());
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
   void *(*eglGetProcAddress)(const char *) = (void*)dlsym(egl, "eglGetProcAddress");

   EGLDisplay dpy = eglGetPlatformDisplay(0x31DD, EGL_DEFAULT_DISPLAY, NULL);
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
      printf("FAIL: egl init\n");
      return 1;
   }
   static const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
   EGLConfig cfg;
   EGLint ncfg = 0;
   eglChooseConfig(dpy, ca, &cfg, 1, &ncfg);
   static const EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
   EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pa);
   static const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
   EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
   if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
      printf("FAIL: make current\n");
      return 1;
   }

   void (*glViewport)(int, int, int, int) = (void*)eglGetProcAddress("glViewport");
   void (*glClearColor)(float, float, float, float) = (void*)eglGetProcAddress("glClearColor");
   void (*glClear)(unsigned) = (void*)eglGetProcAddress("glClear");
   unsigned (*glCreateShader)(unsigned) = (void*)eglGetProcAddress("glCreateShader");
   void (*glShaderSource)(unsigned, int, const char *const *, const int *) = (void*)eglGetProcAddress("glShaderSource");
   void (*glCompileShader)(unsigned) = (void*)eglGetProcAddress("glCompileShader");
   unsigned (*glCreateProgram)(void) = (void*)eglGetProcAddress("glCreateProgram");
   void (*glAttachShader)(unsigned, unsigned) = (void*)eglGetProcAddress("glAttachShader");
   void (*glBindAttribLocation)(unsigned, unsigned, const char *) = (void*)eglGetProcAddress("glBindAttribLocation");
   void (*glLinkProgram)(unsigned) = (void*)eglGetProcAddress("glLinkProgram");
   void (*glUseProgram)(unsigned) = (void*)eglGetProcAddress("glUseProgram");
   void (*glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *) = (void*)eglGetProcAddress("glVertexAttribPointer");
   void (*glEnableVertexAttribArray)(unsigned) = (void*)eglGetProcAddress("glEnableVertexAttribArray");
   void (*glDrawArrays)(unsigned, int, int) = (void*)eglGetProcAddress("glDrawArrays");
   void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *) = (void*)eglGetProcAddress("glReadPixels");
   void (*glFinish)(void) = (void*)eglGetProcAddress("glFinish");

   static const char vs_src[] = "attribute vec4 a_pos;\nvoid main(){gl_Position=a_pos;}\n";
   static const char fs_src[] = "precision mediump float;\nvoid main(){gl_FragColor=vec4(1.,0.,0.,1.);}\n";
   const char *vs_p = vs_src, *fs_p = fs_src;
   unsigned vs = glCreateShader(0x8B31), fs = glCreateShader(0x8B30);
   glShaderSource(vs, 1, &vs_p, NULL); glCompileShader(vs);
   glShaderSource(fs, 1, &fs_p, NULL); glCompileShader(fs);
   unsigned prog = glCreateProgram();
   glAttachShader(prog, vs); glAttachShader(prog, fs);
   glBindAttribLocation(prog, 0, "a_pos");
   glLinkProgram(prog); glUseProgram(prog);

   glViewport(0, 0, W, H);
   glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
   glClear(0x4000);
   static const float verts[] = { -0.8f,-0.8f,0, 0.8f,-0.8f,0, 0.0f,0.8f,0 };
   glVertexAttribPointer(0, 3, 0x1406, 0, 0, verts);
   glEnableVertexAttribArray(0);
   glDrawArrays(0x0004, 0, 3);
   glFinish();
   printf("rendered\n");

   /* ---- X11 SHM present ---- */
   xcb_connection_t *conn = xcb_connect(NULL, NULL);
   if (!conn || xcb_connection_has_error(conn)) {
      printf("FAIL: xcb connect\n");
      return 1;
   }
   const xcb_setup_t *setup = xcb_get_setup(conn);
   xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
   xcb_screen_t *scr = it.data;
   printf("x11 root %ux%u depth %u\n", scr->width_in_pixels, scr->height_in_pixels,
          scr->root_depth);

   xcb_window_t win = xcb_generate_id(conn);
   uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
   uint32_t vals[] = { scr->black_pixel, XCB_EVENT_MASK_EXPOSURE };
   xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root,
                     10, 10, W, H, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     scr->root_visual, mask, vals);
   xcb_map_window(conn, win);
   xcb_flush(conn);
   /* Barrier: wait until the server has mapped the window. */
   xcb_get_input_focus_cookie_t fc = xcb_get_input_focus(conn);
   free(xcb_get_input_focus_reply(conn, fc, NULL));

   xcb_gcontext_t gc = xcb_generate_id(conn);
   uint32_t gmask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
   uint32_t gvals[] = { scr->white_pixel, scr->black_pixel };
   xcb_create_gc(conn, gc, win, gmask, gvals);

   int shmid = shmget(IPC_PRIVATE, W * H * 4, IPC_CREAT | 0600);
   if (shmid < 0) {
      printf("FAIL: shmget\n");
      return 1;
   }
   uint8_t *shm = shmat(shmid, NULL, 0);
   xcb_shm_seg_t seg = xcb_generate_id(conn);
   xcb_shm_attach(conn, seg, shmid, 0);
   shmctl(shmid, IPC_RMID, NULL);

   /* GL RGBA bottom-up -> XRGB top-down for Xvfb x24. */
   static unsigned char rgba[W * H * 4];
   glReadPixels(0, 0, W, H, 0x1908, 0x1401, rgba);
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
         unsigned char *s = rgba + ((H - 1 - y) * W + x) * 4;
         uint8_t *d = shm + (y * W + x) * 4;
         d[0] = s[2];
         d[1] = s[1];
         d[2] = s[0];
         d[3] = 0;
      }
   printf("readback+convert done\n");

   {
      const xcb_query_extension_reply_t *ext =
         xcb_get_extension_data(conn, &xcb_shm_id);
      printf("MIT-SHM present=%d\n", ext && ext->present);
      printf("shm center bytes: %u %u %u %u\n",
             shm[(32 * W + 32) * 4 + 0], shm[(32 * W + 32) * 4 + 1],
             shm[(32 * W + 32) * 4 + 2], shm[(32 * W + 32) * 4 + 3]);
   }
   xcb_void_cookie_t put =
      xcb_shm_put_image_checked(conn, win, gc, W, H, 0, 0, W, H, 0, 0,
                                scr->root_depth, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, seg, 0);
   xcb_flush(conn);
   xcb_generic_error_t *e = xcb_request_check(conn, put);
   printf("put_image error=%d\n", e ? e->error_code : 0);
   {
      xcb_get_window_attributes_cookie_t ac = xcb_get_window_attributes(conn, win);
      xcb_get_window_attributes_reply_t *ar =
         xcb_get_window_attributes_reply(conn, ac, NULL);
      printf("win map_state=%d visual=%u\n",
             ar ? ar->map_state : -1, ar ? ar->visual : 0);
      xcb_translate_coordinates_cookie_t tc =
         xcb_translate_coordinates(conn, win, scr->root, 0, 0);
      xcb_translate_coordinates_reply_t *tr =
         xcb_translate_coordinates_reply(conn, tc, NULL);
      printf("win root pos=(%d,%d) same_screen=%d\n",
             tr ? tr->dst_x : -999, tr ? tr->dst_y : -999,
             tr ? tr->same_screen : -1);
   }
   printf("presented to window 0x%x; check framebuffer\n", win);
   sleep(5);

   {
      xcb_get_image_cookie_t ic =
         xcb_get_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, win, 0, 0, W, H, ~0);
      xcb_flush(conn);
      xcb_get_image_reply_t *ir = xcb_get_image_reply(conn, ic, NULL);
      if (ir) {
         uint8_t *px = xcb_get_image_data(ir);
         printf("server window bytes center: %u %u %u | corner: %u %u %u\n",
                px[(32 * W + 32) * 4 + 0], px[(32 * W + 32) * 4 + 1], px[(32 * W + 32) * 4 + 2],
                px[11 * W * 4 + 11 * 4 + 0], px[11 * W * 4 + 11 * 4 + 1], px[11 * W * 4 + 11 * 4 + 2]);
      } else {
         printf("get_image failed\n");
      }
   }
   xcb_shm_detach(conn, seg);
   shmdt(shm);
   xcb_disconnect(conn);
   return 0;
}
