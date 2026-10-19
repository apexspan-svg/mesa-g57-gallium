# STATE — kbase Gallium on Mali-G57 (compact handoff)

Snapshot: 2026-10-17, after `ca77c8c`. Rootless Termux/Android build of Mesa
Gallium panfrost on kbase (`/dev/mali0`), forked from apexspan
mesa-panvk-mali-g57 v1.3.0.

## CI
- **One push-triggered track: `termux-native-gallium`** ($PREFIX drop-in
  tarball). The NDK track (`android-gallium-kbase`) was dropped in
  `fbada26` — it competed for the same runner capacity and left the
  termux builds queued; the termux artifact is the one that runs on the
  device. The NDK workflow can be restored from git history if needed.
- `probe-termux-docker` is manual-dispatch only.
- Both workflows skip `**.md`-only pushes.
- Last fully green pair: `6b522f1` (termux + android). Also green:
  `913de89` (stage markers), `7b17dda` (EGL device fix), `ca77c8c`,
  `0805e7a`. Red runs seen and fixed: `e22ebce`/`90bfc4a` (compile
  error), `e3c111c` (declaration order).

## Just-landed changes (e22ebce..0805e7a)
- Display-bound batches (SCANOUT/DISPLAYTARGET/SHARED cbufs) always
  drain-then-submit synchronously: swap/present is correct with or without
  async. Offscreen batches stay async. No dma-fence needed yet.
- SPILL_NOOPT retry ported (dormant by design; logs when it fires).
- `PAN_TILER_HEAP_MB=16..2048` (default 128, behavior unchanged).
- DRI3: CreateRegion rejection cached per drawable (`0805e7a`) — verified
  on device: exactly one failure, then clean full-window present;
  interleaved A/B build scene **349/352 → 369/376 (+5.7-7.2%)**.
- `PORTING_APEXSPAN.md`: complete port inventory (ported/next/skipped).

## Driver state
- kbase GL default-on; sync submit default; `PAN_KBASE_ASYNC=1` opt-in.
- Per-BO `kbase_seq` last-writer tracking; reuse/free/fence waits.
- Present: exportable BOs (AHB on unrooted MTK) + DRI3 raw-FD modifier
  1274 under `WSI_X11_TERMUX=1`; plain-Pixmap retry; full-window fallback.
- Teardown/failure paths log named causes.

## Chromium on kbase (in flight)
- Root-caused the GPU-process respawn loop: with `--use-angle=gl` ANGLE
  uses its **native GLX backend**, and it needs `GLX_CONFIG_CAVEAT ==
  GLX_NONE`; the Termux:X11 server's software GLX rates every config
  `GLX_SLOW_CONFIG` and `createDriMode()` copied that rating into the
  client list → zero FBConfigs → `eglInitialize` EGL_NOT_INITIALIZED.
- Fix: `987fd25` (upgrade SLOW → NONE for configs we render on the GPU).
- Driver desktop-GL ceiling: **3.1 compat** by default, **3.2 core** with
  `PAN_MESA_DEBUG=gl3`; 4.x would need geometry + tessellation shaders in
  panfrost. ANGLE lands on **ES 3.1** (ANGLE's ES backend) — the good
  path for WebGL on Mali.
- Repro without chromium: `~/kbase-test/glx_angle_probe.c` (ANGLE's exact
  init), `glx_attr_bisect.c` (names the offending attribute),
  `glx_cfg_dump.c` (per-config attribute dump), `glx_es_probe.c`,
  `glx_cap_probe.c`.

## Numbers (taskset -c 6,7, 640x480, Termux:X11)
- glmark2 full: **226 → 281 → 421 → 352 → 390** (same binaries modulo
  the region-cache fix; spread is thermal/load). 390 ran on `0805e7a`
  right after sustained load; suite stderr now ~11KB (was 12MB).
- glmark2 terrain: 21-27 cool-ish, 15-17 hot; resolution-invariant →
  vertex/tiler-bound. llvmpipe 2 FPS.
- glxgears: 60 locked (vsync) / 350-660 unpaced (llvmpipe 124-150).
- Submit: ~1350us blocked vs ~56us submit-cpu per batch; full suite 118,784
  batches; GPU-bound (~4% CPU recoverable).
- Methodology: cool-down between runs; never compare across thermal states;
  load avg 16-26 typical on this device.

## Open items (ranked)
1. Upstream refresh: re-import main snapshot + replay ~80-commit series
   (base 26.3.0-devel is 2026-10-15; upstream panfrost churns ~15/d incl.
   Kraid v9 compiler fixes). Bring cadence, not urgency.
2. FD semaphore import/export (apexspan v1.2.0): real sync_file fences →
   `fence_get_fd` stops returning -1; EGL native fences; cross-process sync;
   prerequisite for async-default + present pacing.
2. Batch merging across flushes: needs cross-batch dep machinery and a
   workload that actually idles between batches (not yet found).
3. Submit-nowait linear chain (v1.3.0): design input to multi-in-flight.
4. Frame-split instrumentation: GPU-busy% on top of `PAN_KBASE_STATS`.
5. Teardown slowness: intermittent 1-4 min; suspects in `G57_NOTES.md`.
6. `vblank_mode=1` unpaced; transient EGL init failures (now logged).

## Ops quick reference
- EGL test: `LD_LIBRARY_PATH=. LIBGL_DRIVERS_PATH=. MESA_KBASE_NODE=/dev/mali0 EGL_LIB=./libEGL_mesa.so ./egl_kbase_textured`
- GLX test: `DISPLAY=:0 LD_PRELOAD=$HOME/kbase-prefix/lib/libGL.so LD_LIBRARY_PATH=$HOME/kbase-prefix/lib LIBGL_DRIVERS_PATH=$HOME/kbase-prefix/lib/dri WSI_X11_TERMUX=1 glxgears|glmark2`
- X server: `termux-x11 :0 &` from Termux (app alone insufficient). Socket
  can go stale after server death: check `pgrep`, not the socket file.
- Keep screen awake during benches (Developer options "Stay awake" +
  `termux-wake-lock`) or Doze/thermal skews results.
- Flags: `PAN_KBASE_ASYNC=1`, `PAN_KBASE_STATS=1`, `PAN_KBASE_ATOM_STRIDE=56|64|72`,
  `PAN_KBASE_SPLIT=1`, `PAN_SPILL_NOOPT=0`, `PAN_TILER_HEAP_MB=n`.
- Artifacts: GH Actions runs; release `gallium-g57-v1` for recovery.

## Docs
- `G57_NOTES.md`: findings + process lessons.
- `PORTING_APEXSPAN.md`: port inventory (done/next/never + why).
- `STATE.md`: this file.
