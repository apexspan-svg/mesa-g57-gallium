# Port inventory: apexspan mesa-panvk-mali-g57 → Gallium kbase port

Base snapshot: v1.3.0 (`24db5f5`). All paths relative to that tree.
Status as of the async-engine + present work.

## Ported (done, device-tested)
- JM atom layout + stride handling (`panvk_vX_gpu_queue_kbase.c`)
  → `pan_kbase_jm.c`. Core mapping FS vs CS|T, 64-byte stride,
  `PAN_KBASE_ATOM_STRIDE` override. PASS on JM 11.38.
- Split vtc→frag ordering concept → joint submit with ORDER pre-dep
  (fewer round trips than their split path for the same guarantee).
- `PANVK_TILER_HEAP_MB` idea → `PAN_TILER_HEAP_MB` (default unchanged).
- SPILL_NOOPT retry (`panvk_vX_shader.c:1047`) → Gallium retry
  (`PAN_SPILL_NOOPT`, dormant-by-design, fire-logged). Same tls_size
  trigger, same inputs-restore discipline.
- `WSI_X11_TERMUX` + raw-FD 1274 present marker (`panvk_wsi.c`) →
  `PAN_KBASE_RAW_FD_MODIFIER`, forced negotiation, fence-import skip.
- `/dev/mali*` fd detection concept → `loader_fd_is_kbase`,
  `MESA_KBASE_NODE` in surfaceless + android + `x11_dri3_open`
  (their surfaceless equivalent generalized to GLX/EGL-x11/kopper).
- Version-compat notes (FENCE_VALIDATE/STREAM_CREATE surface, stride
  variants) → `G57_NOTES.md` + stride override.

## Ported (adapted, in tree)
- Async engine shape (`panvk_kbase_async.c`: per-device seq, lazy reap,
  no thread, lock never held across polls — with documented single-bag
  deviation) → `pan_kbase_jm.c` async section (`PAN_KBASE_ASYNC`).
  PASS texel-exact; FPS-neutral on measured workloads.
- Per-BO last-writer seq (their teardown imaging) → `kbase_seq` on
  `panfrost_bo`, stamped at submit, waited on reuse/free.
- Fence seq resolution idea → `kbase_seq` fences + `fence_finish` waits.

## To port next (ranked)
1. Batch merging (one job bag per submit): reduces kernel round trips.
   Needs the dep machinery below to stay correct across flushes.
2. FD semaphore import/export (soft-fence atoms, v1.2.0): real
   `sync_file` fences → fixes `fence_get_fd` (-1 today), EGL native
   fences, cross-process sync. Prerequisite for any correct
   async-by-default + present pacing beyond polling.
3. Submit-nowait linear chain (v1.3.0): strict cross-submit ordering
   without CPU waits; design input to multi-in-flight.
4. Frame-split instrumentation (GPU-busy% vs CPU/submit): adopt the
   *method* (we have counters; need busy accounting) before optimizing.
5. Overlap/tiler pipelining: only meaningful with (1)+(2); last.

## Deliberately not porting
- WSI-AFBC experiment: measured slower than LINEAR at 640p; aligns with
  AFBC-off stance. Revisit only with contradictory data.
- SHM/MIT-SHM WSI transport: Vulkan-frontend code; our Gallium present
  uses DRI3 raw-FD + app-side SHM demo instead. Technique reference only.
- AHB swapchain images: same verdict as WSI-AFBC until data says otherwise.
- `PANVK_OVERLAP` heap-split details: Vulkan batch model specific.

## Watch (upstream moves)
- Any `panvk_kbase_async.c` fixes (event races, fence lifetime).
- New kbase uAPI surface mappings (their fence-interface probes).
- Benchmark env (`taskset`, mailbox, NO_AFBC): applies verbatim.
