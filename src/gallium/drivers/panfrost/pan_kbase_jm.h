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

/* Raw-FD present modifier: Termux:X11 and Winlator X servers use 1274 as a
 * private marker for a directly mmap-able raw dma-buf FD. Deliberately not
 * a real DRM format modifier. Resources created with it are linear and
 * allocated exportable. (Protocol from PanVK AHB WSI.) */
#define PAN_KBASE_RAW_FD_MODIFIER 1274ull
