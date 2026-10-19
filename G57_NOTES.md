# Mali-G57 notes (hardware-earned, ours + sister projects)

Environment (Dimensity 6300, Android 13, Termux unprivileged):
- `/dev/mali0` is world-RW (`crw-rw-rw-`): kbase reachable without root.
- `/dev/dri/*` is `system:system`: DRM present/prime paths are unusable.
  Every display path must avoid DRM (SHM or raw-FD dma-buf).
- kbase JM uAPI 11.38 here; r49-era kernels differ (see stride).

## Verified in our tree (device-tested)
- EGL surfaceless init on `/dev/mali0` via `MESA_KBASE_NODE`.
- Clear, flat-shaded triangle, textured quad (texel-exact) — all PASS.
- GLX direct context (`direct=1`, correct renderer) via `x11_dri3_open`
  fallback; no env vars needed for device acquisition.
- Visible triangle + textured quad in X11 windows (SHM present, app-side).
- Tiler hierarchy mask: upstream selection, no `0xFE` tuning → no `0x58`
  class from HapplyCardo.
- v9 `MALLOC_VERTEX` VARYING block: upstream rewrite is sound; textured
  PASS covers the varying path HapplyCardo's bug lived in.
- Async + display-bound submit gating (`ca77c8c`): EGL textured PASS with
  `PAN_KBASE_ASYNC=1`; glxgears runs on X11 under async (CreateRegion
  fallback present, no crash).

## Loader hooks (all gated, no-op without kbase/MESA_KBASE_NODE)
- `loader_fd_is_kbase()` (`src/loader/loader.c`): `/proc/self/fd` sniffing
  maps `/dev/mali*` to driver `panfrost`.
- `MESA_KBASE_NODE` in surfaceless + android EGL probe (JimVulkan pattern).
- `x11_dri3_open` kbase fallback (GLX/EGL-x11/kopper in one).

## Present findings (Termux:X11 server)
- Server offers ZERO window modifiers (probed) yet imports 1274-marked
  raw dma-buf FDs (PanVK-proven; the marker is deliberately unadvertised).
- Server rejects `PixmapFromBuffers` with standard modifiers (BadRequest).
- Server rejects `xcb_xfixes_create_region` despite advertising XFixes
  5.0: damage regions must be optional with full-present fallback. Since
  `0805e7a` the rejection is cached per drawable — one failed attempt at
  first swap, then full-window present without per-swap round trips or
  spew (previously ~7k waste-lines per 30-scene suite).
- No DRI3 explicit sync on raw path: driver must be GPU-idle before
  queueing the pixmap (true for our synchronous submit).
- Xvfb `-fbdir` files do NOT reflect SHM-put content; verify windows with
  `GetImage`, not the fb file.

## Submit design (current: synchronous)
- One `JOB_SUBMIT` per batch; frag `ORDER`-depends on vtc (panfork-style,
  halves kernel round trips vs split+wait). `PAN_KBASE_SPLIT=1` restores
  split submits for kernels whose ORDER deps misbehave.
- `core_req`: frag `FS`, vtc `CS|T` (PanVK-proven on G57). JimVulkan's
  traced values (`0x4e`/`0x8001`) kept as alternative, untested here.
- One process-wide submit mutex: batch order on the wire equals program
  order; event matching unambiguous; 8-bit atom wrap impossible.
- Fences truthfully signaled (submit blocks). Async port must use `>=`
  seq comparison (HapplyCardo `4c42d497`) and per-BO tracking.
- Async engine (`PAN_KBASE_ASYNC=1`, opt-in): per-device seq, lazy reap;
  display-bound batches (SCANOUT/DISPLAYTARGET/SHARED cbufs) drain
  outstanding async then submit synchronously → present correct by
  construction, no dma-fences needed. Offscreen batches stay async.
  Threadless; lock never held across polls.

## Desktop GL version ceiling (measured 2026-10-19)
- Default build exposes **desktop GL 3.1 compat** (GLSL 1.40) and **GL ES
  3.1**; `GL_VERSION=3.1 Mesa 26.3.0-devel`, `GL_RENDERER=Mali-G57 MC2
  (Panfrost)`.
- `PAN_MESA_DEBUG=gl3` (fork's "experimental GL 3.x implementation, up to
  3.3", `pan_screen.c:816` — `caps->glsl_feature_level = is_gl3 ? 330 :
  140`) lifts the desktop side to **GL 3.2 core** (GLSL 150) and exposes
  `MAX_VERTEX_OUTPUT_COMPONENTS=128`. 4.x is not reachable: desktop GL
  3.2+ mandates a geometry shader and 4.0 tessellation, and panfrost
  implements neither (`MAX_GEOMETRY_SHADER_INVOCATIONS`,
  `MAX_PATCH_VERTICES` unsupported) — a driver-feature gap, not a kbase
  hardware limit.
- Consequence for ANGLE: the **ES 3.1** path is the good one (ANGLE's ES
  backend, ESSL→ESSL). Forcing desktop GL (the `gl3` flag) would push
  ANGLE onto its GL backend (ESSL→GLSL 150 + emulating ES features
  desktop GL 3.2 lacks) — slower and feature-poorer. Keep `gl3` off for
  Chromium; it is a desktop-app convenience.
- Caps present at 3.1: uniform blocks (15/66/105), 16 vertex attribs, 32
  texture image units, 8 draw buffers, 8 samples, 128 TF attribs, 8 shader
  storage blocks. Missing: GS, tessellation, compute work groups.

## Chromium / ANGLE on kbase (the GPU-process respawn loop)
- Symptom: chromium's GPU process dies at once with
  `ANGLE Display::initialize error 12289: Could not create a backing OpenGL
  context` → `eglInitialize OpenGL failed with error EGL_NOT_INITIALIZED`
  → `Exiting GPU process due to errors during initialization`; the browser
  respawns it in a loop and falls back to software (~1 fps).
- Decisive fact: with `--use-angle=gl` ANGLE uses its **native GLX
  backend** (`libANGLE/renderer/gl/glx/DisplayGLX.cpp`), *not* our EGL.
  Every EGL-side fix (kopper, DRI3, the kbase EGLDevice) is therefore
  irrelevant to this failure — the *client-side GLX FBConfig list* is
  what ANGLE consumes. Our EGL path was innocent the whole time.
- ANGLE's FBConfig query (exact list in DisplayGLX.cpp) demands RGBA8 +
  depth 24 + stencil 8 + doublebuffer + WINDOW|PBUFFER|PIXMAP +
  X_RENDERABLE + **GLX_CONFIG_CAVEAT == GLX_NONE**.
- Our `dri_util.c` reported `__DRI_ATTRIB_SLOW_BIT` for every config that
  carries an accumulation buffer; `fbconfigs_compatible()` maps that to
  GLX_SLOW_CONFIG (0x8000), so ANGLE's `GLX_NONE` request rejected
  *every* config → "Could not find a decent GLX FBConfig to create the
  context." → EGL_NOT_INITIALIZED. Fix (`6b522f1`): report caveat 0 —
  the kbase device is a hardware accelerator (upstream Mesa reports 0
  here too).
- ANGLE's context ladder (`GenerateContextCreationToTry`): desktop core
  4.5→3.2, desktop legacy 3.3, then ES 3.2→2.0. Its GL backend also
  refuses desktop GL < 3.2 ("Desktop GL below 3.2 is not supported"), and
  our tree tops out at GL 3.1 *compat* → ANGLE has to land on an ES
  context, which our GLX supports (`GLX_CONTEXT_ES_PROFILE_BIT_EXT` maps
  to `__DRI_API_GLES2/3`). Measured on device: our GLX creates **GL ES
  3.1** (`GL_RENDERER = Mali-G57 MC2 (Panfrost)`, "OpenGL ES 3.1 Mesa
  26.3.0-devel") with a 1x1 pbuffer + makeCurrent; ES 3.2 is refused
  (GLXBadFBConfig) and desktop core ≥3.2 is refused (EGL_BAD_MATCH on the
  EGL side). So ANGLE runs its **ES backend** on kbase — the WebGL
  translation tax applies, but the GPU work is ours.
- Reproducer without chromium: `~/kbase-test/glx_angle_probe.c` replays
  ANGLE's init verbatim (its exact FBConfig attribute list, then its
  context ladder) — the cheapest way to test this class of bug; the
  `glx_cfg_dump.c` companion dumps every config attribute (n, caveat,
  stencil, drawable bits) when a query returns nothing.
- Debugging lessons: chromium's message names "backing OpenGL context",
  never the FBConfig attribute that failed, so the failing requirement
  has to be reconstructed from ANGLE's source. `--gpu-launcher=` is a
  **no-op** in this build (155) — a wrapper script never ran. The GPU
  child has no `DISPLAY` in its env (ozone shares the X connection over
  mojo), so DISPLAY is not the missing piece. `MESA-LOADER: failed to
  retrieve device information` in the GPU process is expected on kbase
  (no DRM device) and harmless.

## Adopt when relevant
- AFBC: disable on G57 kbase until proven (`PANVK_NO_AFBC=1` precedent;
  `PAN_BASE_QUIRK_NO_AFBC`). Suspect for any future `DATA_INVALID_FAULT`
  in textured scenes.
- Big-core pinning (`taskset -c 6,7`, +2–7%) for every benchmark.
- Native kbase `MEM_ALLOC` BOs can NEVER be exported; presentable resources
  need dma-heap/AHB backing from birth (`PAN_BO_EXPORTABLE`).
- Our targets: glmark2 ≥143 (apexspan Zink number, minus translation tax).

## Known issues
- Slow process teardown (1-4 min after score print) on heavy runs, with
  `KBASE_IOCTL_MEM_FREE Operation not permitted` noise. Renders/benchmarks
  unaffected (score prints first). Suspects: per-BO teardown cost at
  volume, SAME_VA free-after-munmap ordering, or shm-fence waits.
  Repro: full glmark2; single scene exits clean. Small-BO exits instant.
- `PAN_KBASE_STATS=1` baseline: ~1.4-1.5ms blocked per batch (includes
  GPU execution under synchronous submit; cf. PanVK 156us driver CPU).
- Dead X server leaves a stale `X0` socket in `$PREFIX/tmp/.X11-unix`:
  clients fail "couldn't open display :0" until `termux-x11 :0 &` is
  re-run. Check `pgrep termux-x11`, not the socket file.

## Measured (Dimensity 6300, Termux:X11, taskset -c 6,7, 640x480)
- glmark2 Score: **226**, later **281**, later **421**, later **352**
  (hot — straight after a prior full suite + A/B runs), later **390**
  with the region-cache fix under equal-or-worse heat — all scenes
  complete, no crashes. Scene range ~538 top / 23 bottom. Variance across runs is thermal/load,
  not code: same binaries score differently hot vs cool. Always
  `taskset -c 6,7`. Suite stderr is ~11KB since the region fix
  (was 12MB).
- glxgears: vsync-locked ~60 FPS sustained (swap paced, no errors).
- DRI3 region-cache A/B (`0805e7a`, build scene, interleaved old/new to
  cancel heat): old 349/352 vs **new 369/376 → +5.7-7.2%**, new-run-heat
  handicap included. Present output identical (full-window either way).
- Suite-wide: 352 on `ca77c8c` (33/33, clean exit) — the prior per-swap
  region attempts are one source of light-scene cost.
- SPILL retry never fires on glmark2 (no spilling shaders there) nor on a
  synthetic 8-varying stress shader: dormant as designed, no regressions.
- Teardown slowness is intermittent (minutes some runs, prompt others);
  still under investigation, benchmarks unaffected.

## Upstream cadence (Mesa)
- Base: `26.3.0-devel` snapshot imported 2026-10-15 (squash commit
  `95465cd`, our kbase series on top — `670e13e` + small follow-ups).
  That is fresher than latest stable (26.2.5, Oct 14).
- Upstream panfrost/panvk churn since import: ~15 commits per day, incl.
  Valhall **Kraid compiler** fixes (v9 = our GPU), `Use full texture
  handles instead of offsets on v9+` (gallium + panvk), pan/lib bugfixes.
  We drift daily; a snapshot, not live tracking.
- Refresh procedure: re-import fresh main snapshot + replay our series.
  Cadence: weekly or after a big v9 upstream landing — never mid-
  experiment (would conflate code and thermal deltas).

## kbase version notes
- Atom stride is kernel-version dependent (BossDrk, G52/r49: 56 bytes vs
  the usual 64). Default 64 (proven JM 11.38); override
  `PAN_KBASE_ATOM_STRIDE=56|64|72`; FristOneRR auto-detects.

## Process lessons
- Verify CI after **every** push. e22ebce + 90bfc4a sat red unnoticed
  (compile error, not a flake) until a status sweep caught it.- This tree stores `pipe_framebuffer_state.cbufs` **by value**
  (`struct pipe_surface cbufs[]`, no pointers): access
  `.cbufs[i].texture` directly.
- Docs pushes rebuilt both tracks (android ignored only README.md;
  termux had no filter). Both workflows now ignore `**.md`.
- Redirected stdout is block-buffered: a killed glxgears prints no FPS
  lines. Read FPS from programs that exit normally (glmark2).

## Sources
- HapplyCardo/mesa-Panfork-android `bifrost-g57` (`d8cb29f9`, `4c42d497`).
- JimVulkan/mali-panfrost: traced `core_req`, async/event design.
- apexspan/mesa-panvk-mali-g57: PanVK JM backend, X11/WSI reference.
- termux/termux-docker#62 + guijan/robertkirkman termux-on-gha: CI pattern.
