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
- Server rejects `xcb_xfixes_create_region` despite advertising XFixes 5.0:
  damage regions must be optional with full-present fallback.
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
- glmark2 Score: **226**, later **281**, later **421** (vs 143 Zink-on-PanVK),
  all scenes complete, no crashes. Scene range now ~370-600 FPS light,
  heavy scenes 23-123 FPS. Variance across runs is thermal/load, not code:
  same binaries score differently hot vs cool. Always `taskset -c 6,7`.
- glxgears: vsync-locked ~60 FPS sustained (swap paced, no errors).
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
  (compile error, not a flake) until a status sweep caught it.
- This tree stores `pipe_framebuffer_state.cbufs` **by value**
  (`struct pipe_surface cbufs[]`, no pointers): access
  `.cbufs[i].texture` directly.
- Docs pushes rebuilt both tracks (android ignored only README.md;
  termux had no filter). Both workflows now ignore `**.md`.

## Sources
- HapplyCardo/mesa-Panfork-android `bifrost-g57` (`d8cb29f9`, `4c42d497`).
- JimVulkan/mali-panfrost: traced `core_req`, async/event design.
- apexspan/mesa-panvk-mali-g57: PanVK JM backend, X11/WSI reference.
- termux/termux-docker#62 + guijan/robertkirkman termux-on-gha: CI pattern.
