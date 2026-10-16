/*
 * Gallium Panfrost JM-over-kbase shim (prototype v1, Mali-G57 / Valhall v9).
 *
 * Routes JM job-chain submission to the vendor kbase kernel driver
 * (/dev/mali0, KBASE_IOCTL_JOB_SUBMIT) instead of the mainline
 * DRM_IOCTL_PANFROST_SUBMIT. Synchronous: each submit blocks until the
 * JD DONE event arrives, so Gallium fences can report signaled immediately.
 *
 * Mirrors the proven PanVK path in src/panfrost/vulkan/jm/panvk_vX_gpu_queue_kbase.c:
 * split vtc/frag submits, core_req CS|T for vertex/tiler and FS for fragment.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct panfrost_device;
struct panfrost_batch;

/* True when this Gallium device sits on the kbase kmod backend. */
bool pan_kbase_jm_is_kbase(const struct panfrost_device *dev);

/* Submit one JM job chain synchronously via kbase. Returns 0 on success,
 * errno value on failure. reqs uses PANFROST_JD_REQ_* bits. */
int pan_kbase_jm_submit_jc(struct panfrost_device *dev, uint64_t first_job_desc,
                           uint32_t reqs);

/* Submit a batch's vtc + fragment chains, chaining frag after vtc with an
 * ORDER pre-dep in a single JOB_SUBMIT. Either jc may be 0 (skipped).
 * Returns 0 on success, errno value on failure. */
int pan_kbase_jm_submit_batch(struct panfrost_device *dev, uint64_t vtc_jc,
                              uint64_t frag_jc, uint32_t vtc_reqs,
                              uint32_t frag_reqs);

/* ---- Async engine (PAN_KBASE_ASYNC) ----
 *
 * Synchronous submit blocks the CPU in poll() for the whole GPU execution
 * of every batch. The async engine returns from submit immediately after
 * the kernel accepts the atoms and reaps completion lazily at the next
 * natural synchronization point (fence, CPU map, fd export, teardown).
 * Ordering is preserved by keeping a single outstanding bag per process
 * (same serialization as the sync path, cf. PanVK PANVK_ASYNC): the next
 * submit waits for the previous bag, so the kernel sees identical order
 * while CPU batch preparation overlaps GPU execution.
 *
 * Sequence numbers identify submissions; resources remember the seq of
 * their last writer so reuse waits precisely. Present (swap) is NOT
 * synchronized in this phase: with async on, a swap may display a
 * not-yet-complete buffer (tearing). Benchmarks are unaffected; visual
 * correctness of swap returns in Phase C (present waits / dma-fence).
 */
bool pan_kbase_async_enabled(void);

/* Submit without waiting. Returns the submission seq, 0 on kernel refusal
 * (nothing recorded then). Degenerate (one/both jc zero) handled. */
uint64_t pan_kbase_async_submit_batch(struct panfrost_device *dev,
                                      uint64_t vtc_jc, uint64_t frag_jc,
                                      uint32_t vtc_reqs, uint32_t frag_reqs);

/* True when seq is known-complete. */
bool pan_kbase_async_seq_done(uint64_t seq);

/* Block until seq completes (or timeout_ns elapses). Seq 0 (never
 * submitted) completes immediately. Returns true on completion. */
bool pan_kbase_async_wait_seq(struct panfrost_device *dev, uint64_t seq,
                              int64_t timeout_ns);

/* Last sequence number handed out (0 if none). */
uint64_t pan_kbase_async_last_seq(void);

/* Raw-FD present modifier: Termux:X11 and Winlator X servers use 1274 as a
 * private marker for a directly mmap-able raw dma-buf FD. Deliberately not
 * a real DRM format modifier. Resources created with it are linear and
 * allocated exportable. (Protocol from PanVK AHB WSI.) */
#define PAN_KBASE_RAW_FD_MODIFIER 1274ull
