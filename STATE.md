# STATE — kbase Gallium on Mali-G57 (compact handoff)

Snapshot: 2026-10-17, after `ca77c8c`. Rootless Termux/Android build of Mesa
Gallium panfrost on kbase (`/dev/mali0`), forked from apexspan
mesa-panvk-mali-g57 v1.3.0.

## CI
- Two tracks: `android-gallium-kbase` (NDK zip) + `termux-native-gallium`
  ($PREFIX drop-in tarball). Both auto-run on pushes to main.
- Last fully green pair: `db260e6`. `e296872`: termux green (android unchecked).
- `e22ebce` + `90bfc4a`: RED (compile error, by-value cbufs). Fixed in
  `ca77c8c`; verification watch was running at snapshot time.
- Both workflows now skip `**.md`-only pushes.

## Just-landed changes (e22ebce..ca77c8c)
- Display-bound batches (SCANOUT/DISPLAYTARGET/SHARED cbufs) always
  drain-then-submit synchronously: swap/present is correct with or without
  async. Offscreen batches stay async. No dma-fence needed yet.
- SPILL_NOOPT retry ported (dormant by design; logs when it fires).
- `PAN_TILER_HEAP_MB=16..2048` (default 128, behavior unchanged).
- `PORTING_APEXSPAN.md`: complete port inventory (ported/next/skipped).

## Driver state
- kbase GL default-on; sync submit default; `PAN_KBASE_ASYNC=1` opt-in.
- Per-BO `kbase_seq` last-writer tracking; reuse/free/fence waits.
- Present: exportable BOs (AHB on unrooted MTK) + DRI3 raw-FD modifier
  1274 under `WSI_X11_TERMUX=1`; plain-Pixmap retry; full-window fallback.
- Teardown/failure paths log named causes.

## Numbers (taskset -c 6,7, 640x480, Termux:X11)
- glmark2 full: **226 → 281 → 421** (same binaries; spread is thermal/load).
- glmark2 terrain: 21-27 cool-ish, 15-17 hot; resolution-invariant →
  vertex/tiler-bound. llvmpipe 2 FPS.
- glxgears: 60 locked (vsync) / 350-660 unpaced (llvmpipe 124-150).
- Submit: ~1350us blocked vs ~56us submit-cpu per batch; full suite 118,784
  batches; GPU-bound (~4% CPU recoverable).
- Methodology: cool-down between runs; never compare across thermal states;
  load avg 16-26 typical on this device.

## Open items (ranked)
1. FD semaphore import/export (apexspan v1.2.0): real sync_file fences →
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
- X server: `termux-x11 :0 &` from Termux (app alone insufficient).
- Keep screen awake during benches (Developer options "Stay awake" +
  `termux-wake-lock`) or Doze/thermal skews results.
- Flags: `PAN_KBASE_ASYNC=1`, `PAN_KBASE_STATS=1`, `PAN_KBASE_ATOM_STRIDE=56|64|72`,
  `PAN_KBASE_SPLIT=1`, `PAN_SPILL_NOOPT=0`, `PAN_TILER_HEAP_MB=n`.
- Artifacts: GH Actions runs; release `gallium-g57-v1` for recovery.

## Docs
- `G57_NOTES.md`: findings + process lessons.
- `PORTING_APEXSPAN.md`: port inventory (done/next/never + why).
- `STATE.md`: this file.
