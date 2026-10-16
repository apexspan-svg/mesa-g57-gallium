/*
 * Gallium Panfrost JM-over-kbase submit (v2).
 *
 * Routes JM job chains to the vendor kbase driver (/dev/mali0,
 * KBASE_IOCTL_JOB_SUBMIT) instead of DRM_IOCTL_PANFROST_SUBMIT.
 *
 * Design notes:
 * - Atom layout cross-checked against the canonical kbase UAPI
 *   (panfork's mali_base_jm_kernel.h: struct base_jd_atom_v2 — jc, udata,
 *   extres, pre_dep, atom_number, core_req; DEP_ORDER=2, FS=1<<0,
 *   CS=1<<1, T=1<<2). Our 64-byte struct keeps 16 zeroed tail bytes where
 *   canonical has renderpass_id+padding (8); the tail is ignored by the
 *   kernel and 64 is the stride proven by PanVK on G57 kbase. 56/72 remain
 *   selectable via PAN_KBASE_ATOM_STRIDE (cf. FristOneRR's G52/r49 finding).
 * - Dependency tracking follows icecream95's panfork kbase_submit():
 *   vtc and frag atoms of one batch go out in a single JOB_SUBMIT with the
 *   fragment carrying an ORDER pre-dep on the vtc atom, so the kernel —
 *   not a CPU round trip — enforces tiler-before-fragment. Cross-batch
 *   ordering comes from holding one device-global submit mutex across
 *   submit+wait (same "one bag in flight" serialization apexspan uses).
 * - Syscalls used: ioctl(JOB_SUBMIT) + poll/read(events) only. No DRM
 *   syncobjs, no GEM handles: residency comes from the kbase VM.
 * - v3 (async event pump + per-BO use_count release, panfork-style) is
 *   future work; it needs BO-lifetime integration first. Until then every
 *   submit is synchronous, so a fence may truthfully report signaled.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pan_kbase_jm.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "drm-uapi/panfrost_drm.h"
#include "pan_device.h"
#include "util/log.h"
#include "util/os_time.h"

#include <pthread.h>

#include "kmod/pan_kmod.h"

/* Declared in kbase_kmod.h (gallium-kbase-jm-v1 addition). */
bool pan_kmod_dev_is_kbase(const struct pan_kmod_dev *dev);

#define KBASE_IOCTL_TYPE 0x80

struct kbase_ioctl_job_submit {
   uint64_t addr;
   uint32_t nr_atoms;
   uint32_t stride;
};
#define KBASE_IOCTL_JOB_SUBMIT \
   _IOW(KBASE_IOCTL_TYPE, 2, struct kbase_ioctl_job_submit)

struct base_jd_event_v2 {
   uint32_t event_code;
   uint8_t atom_number;
   uint8_t padding[3];
   uint64_t udata[2];
};

enum {
   BASE_JD_EVENT_DONE = 0x01,
};

/* Canonical values (mali_base_jm_kernel.h). */
#define BASE_JD_DEP_TYPE_ORDER ((uint8_t)(1u << 1))
#define BASE_JD_REQ_FS         ((uint32_t)1 << 0)
#define BASE_JD_REQ_CS         ((uint32_t)1 << 1)
#define BASE_JD_REQ_T          ((uint32_t)1 << 2)

struct base_dependency {
   uint8_t atom_id;
   uint8_t dependency_type;
} __attribute__((packed));

struct base_jd_atom_v2 {
   uint64_t jc;
   uint64_t udata[2];
   uint64_t extres_list;
   uint16_t nr_extres;
   uint8_t jit_id[2];
   struct base_dependency pre_dep[2];
   uint8_t atom_number;
   uint8_t prio;
   uint8_t device_nr;
   uint8_t jobslot;
   uint32_t core_req;
   uint8_t payload[16];
} __attribute__((packed, aligned(16)));

/* Submit statistics (PAN_KBASE_STATS=1): batches submitted, atoms, and
 * total microseconds spent blocked in submit+wait. Printed to stderr at
 * every 512th batch and on the first error. */
struct kbase_submit_stats {
   uint64_t batches;
   uint64_t atoms;
   uint64_t wait_us;
};

static struct kbase_submit_stats submit_stats;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool
stats_enabled(void)
{
   static int enabled = -1;
   if (enabled < 0)
      enabled = getenv("PAN_KBASE_STATS") != NULL;
   return enabled;
}

static void
stats_add(unsigned atoms, uint64_t wait_ns)
{
   if (!stats_enabled())
      return;
   pthread_mutex_lock(&stats_mutex);
   submit_stats.batches++;
   submit_stats.atoms += atoms;
   submit_stats.wait_us += wait_ns / 1000;
   if ((submit_stats.batches & 511) == 0) {
      fprintf(stderr,
              "panfrost/kbase stats: %llu batches, %llu atoms, "
              "%llu us blocked (avg %.1f us/batch)\n",
              (unsigned long long)submit_stats.batches,
              (unsigned long long)submit_stats.atoms,
              (unsigned long long)submit_stats.wait_us,
              (double)submit_stats.wait_us / (double)submit_stats.batches);
   }
   pthread_mutex_unlock(&stats_mutex);
}

/* One submit+wait at a time per process: with the mutex held, no two
 * threads can interleave atoms, so event matching is unambiguous and batch
 * order on the wire equals program order. */
static pthread_mutex_t submit_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t next_atom = 1;

bool
pan_kbase_jm_is_kbase(const struct panfrost_device *dev)
{
   return dev && dev->kmod.dev && pan_kmod_dev_is_kbase(dev->kmod.dev);
}

static uint32_t
atom_stride(void)
{
   const char *env = getenv("PAN_KBASE_ATOM_STRIDE");
   if (env && env[0]) {
      long v = strtol(env, NULL, 0);
      if (v == 56 || v == 64 || v == 72)
         return (uint32_t)v;
   }
   return sizeof(struct base_jd_atom_v2);
}

/* PAN_KBASE_SPLIT=1 forces the v1 behaviour (one JOB_SUBMIT per chain with
 * a CPU wait between vtc and frag) for kernels whose ORDER deps misbehave. */
static bool
force_split(void)
{
   return getenv("PAN_KBASE_SPLIT") != NULL;
}

static int
wait_atom(int fd, uint8_t atom_number, int64_t timeout_ns)
{
   int64_t deadline = os_time_get_nano() + timeout_ns;

   while (true) {
      int64_t remaining = deadline - os_time_get_nano();
      if (remaining <= 0) {
         mesa_loge("panfrost/kbase: timed out waiting for atom %u",
                   atom_number);
         return ETIMEDOUT;
      }

      struct pollfd pfd = {.fd = fd, .events = POLLIN};
      int ret = poll(&pfd, 1, (int)((remaining + 999999) / 1000000));
      if (ret < 0) {
         if (errno == EINTR)
            continue;
         return errno;
      }
      if (ret == 0)
         return ETIMEDOUT;
      if (!(pfd.revents & POLLIN))
         continue;

      struct base_jd_event_v2 ev;
      ssize_t n = read(fd, &ev, sizeof(ev));
      if (n < 0) {
         if (errno == EINTR || errno == EAGAIN)
            continue;
         return errno;
      }
      if ((size_t)n < sizeof(ev))
         continue;

      /* Mutex serializes submit+wait, so a foreign atom id here would mean
       * a kernel-side emitter (e.g. JIT/soft-job); keep waiting for ours. */
      if (ev.atom_number != atom_number)
         continue;

      if (ev.event_code == BASE_JD_EVENT_DONE)
         return 0;

      mesa_loge("panfrost/kbase: atom %u failed, event 0x%x", atom_number,
                ev.event_code);
      return EIO;
   }
}

/* Allocate the next atom id in 1..255 (0 is reserved on some kbase builds).
 * Caller must hold submit_mutex. */
static uint8_t
alloc_atom(void)
{
   uint8_t id = next_atom++;
   if (id == 0)
      id = next_atom++;
   return id;
}

static uint32_t
core_req_for(uint32_t reqs)
{
   /* Same mapping as PanVK on Valhall v9: fragment chain on the FS slot,
    * everything else (vertex/tiler/compute) on CS|T. */
   return (reqs & PANFROST_JD_REQ_FS) ? BASE_JD_REQ_FS
                                      : (BASE_JD_REQ_CS | BASE_JD_REQ_T);
}

int
pan_kbase_jm_submit_jc(struct panfrost_device *dev, uint64_t first_job_desc,
                       uint32_t reqs)
{
   pthread_mutex_lock(&submit_mutex);

   struct base_jd_atom_v2 atom = {
      .jc = first_job_desc,
      .atom_number = alloc_atom(),
      .core_req = core_req_for(reqs),
   };

   struct kbase_ioctl_job_submit sub = {
      .addr = (uint64_t)(uintptr_t)&atom,
      .nr_atoms = 1,
      .stride = atom_stride(),
   };

   int ret = ioctl(dev->kmod.dev->fd, KBASE_IOCTL_JOB_SUBMIT, &sub);
   if (ret) {
      int err = errno;
      mesa_loge("panfrost/kbase: JOB_SUBMIT failed (stride=%u): %s", sub.stride,
                strerror(err));
      pthread_mutex_unlock(&submit_mutex);
      return err;
   }

   int64_t t0 = os_time_get_nano();
   ret = wait_atom(dev->kmod.dev->fd, atom.atom_number, 60 * 1000000000ll);
   stats_add(1, os_time_get_nano() - t0);
   pthread_mutex_unlock(&submit_mutex);
   return ret;
}

int
pan_kbase_jm_submit_batch(struct panfrost_device *dev, uint64_t vtc_jc,
                          uint64_t frag_jc, uint32_t vtc_reqs,
                          uint32_t frag_reqs)
{
   /* Degenerate cases fall back to single submits. */
   if (!vtc_jc)
      return frag_jc ? pan_kbase_jm_submit_jc(dev, frag_jc, frag_reqs) : 0;
   if (!frag_jc)
      return pan_kbase_jm_submit_jc(dev, vtc_jc, vtc_reqs);

   if (force_split()) {
      int ret = pan_kbase_jm_submit_jc(dev, vtc_jc, vtc_reqs);
      if (ret)
         return ret;
      return pan_kbase_jm_submit_jc(dev, frag_jc, frag_reqs);
   }

   pthread_mutex_lock(&submit_mutex);

   /* Joint submit, panfork-style: frag ORDER-depends on vtc so the kernel
    * sequences tiler-before-fragment without a CPU round trip. */
   struct base_jd_atom_v2 atoms[2] = {
      {
         .jc = vtc_jc,
         .atom_number = alloc_atom(),
         .core_req = core_req_for(vtc_reqs),
      },
      {
         .jc = frag_jc,
         .core_req = core_req_for(frag_reqs),
      },
   };
   atoms[1].atom_number = alloc_atom();
   atoms[1].pre_dep[0].atom_id = atoms[0].atom_number;
   atoms[1].pre_dep[0].dependency_type = BASE_JD_DEP_TYPE_ORDER;

   struct kbase_ioctl_job_submit sub = {
      .addr = (uint64_t)(uintptr_t)&atoms,
      .nr_atoms = 2,
      .stride = atom_stride(),
   };

   int ret = ioctl(dev->kmod.dev->fd, KBASE_IOCTL_JOB_SUBMIT, &sub);
   if (ret) {
      int err = errno;
      mesa_loge("panfrost/kbase: joint JOB_SUBMIT failed (stride=%u): %s",
                sub.stride, strerror(err));
      pthread_mutex_unlock(&submit_mutex);
      return err;
   }

   /* Either atom failing fails the batch; wait for both (vtc first, since
    * frag cannot complete before it). */
   int64_t t0 = os_time_get_nano();
   ret = wait_atom(dev->kmod.dev->fd, atoms[0].atom_number,
                   60 * 1000000000ll);
   if (!ret)
      ret = wait_atom(dev->kmod.dev->fd, atoms[1].atom_number,
                      60 * 1000000000ll);
   stats_add(2, os_time_get_nano() - t0);
   pthread_mutex_unlock(&submit_mutex);
   return ret;
}

/* ---- Async engine ---- */

struct kbase_async_bag {
   bool valid;
   uint64_t seq;
   struct base_jd_atom_v2 atoms[2];
   unsigned nr;
};

static struct {
   pthread_mutex_t lock;
   bool enabled_init;
   bool enabled;
   uint64_t seqno;
   uint64_t completed;
   struct kbase_async_bag pending;
} async_state = {
   .lock = PTHREAD_MUTEX_INITIALIZER,
};

bool
pan_kbase_async_enabled(void)
{
   if (!async_state.enabled_init) {
      async_state.enabled_init = true;
      async_state.enabled = getenv("PAN_KBASE_ASYNC") != NULL;
   }
   return async_state.enabled;
}

uint64_t
pan_kbase_async_last_seq(void)
{
   pthread_mutex_lock(&async_state.lock);
   uint64_t seq = async_state.seqno;
   pthread_mutex_unlock(&async_state.lock);
   return seq;
}

bool
pan_kbase_async_seq_done(uint64_t seq)
{
   if (seq == 0)
      return true;
   pthread_mutex_lock(&async_state.lock);
   bool done = seq <= async_state.completed;
   pthread_mutex_unlock(&async_state.lock);
   return done;
}

/* Caller must hold async_state.lock. Waits for the pending bag with the
 * remaining budget; on success retires it. Lock is held across the poll
 * (event matching is unambiguous: a single bag is ever outstanding). */
static bool
wait_pending_locked(struct panfrost_device *dev, int64_t timeout_ns)
{
   struct kbase_async_bag *bag = &async_state.pending;
   int64_t deadline = os_time_get_nano() + timeout_ns;

   for (unsigned i = 0; i < bag->nr; i++) {
      int64_t remaining = deadline - os_time_get_nano();
      if (remaining <= 0)
         return false;
      int ret =
         wait_atom(dev->kmod.dev->fd, bag->atoms[i].atom_number, remaining);
      if (ret) {
         /* A faulted atom will never complete: retire the bag so later
          * waits do not spin on it. Completion stays behind, so
          * dependent waits still report failure via their own path. */
         async_state.completed = bag->seq;
         bag->valid = false;
         return false;
      }
   }

   async_state.completed = bag->seq;
   bag->valid = false;
   return true;
}

bool
pan_kbase_async_wait_seq(struct panfrost_device *dev, uint64_t seq,
                         int64_t timeout_ns)
{
   if (seq == 0)
      return true;

   pthread_mutex_lock(&async_state.lock);
   if (seq <= async_state.completed) {
      pthread_mutex_unlock(&async_state.lock);
      return true;
   }
   if (!async_state.pending.valid) {
      /* Unknown seq (e.g. submit failed after handing it out — cannot
       * happen: failures return 0 and record nothing). */
      pthread_mutex_unlock(&async_state.lock);
      return false;
   }
   /* With one bag outstanding, the pending bag is the newest; waiting for
    * it completes everything up to and including seq. */
   bool ok = wait_pending_locked(dev, timeout_ns);
   pthread_mutex_unlock(&async_state.lock);
   return ok;
}

uint64_t
pan_kbase_async_submit_batch(struct panfrost_device *dev, uint64_t vtc_jc,
                             uint64_t frag_jc, uint32_t vtc_reqs,
                             uint32_t frag_reqs)
{
   if (!vtc_jc && !frag_jc)
      return pan_kbase_async_last_seq();

   pthread_mutex_lock(&async_state.lock);

   /* Preserve kernel order: never have two bags in flight. */
   if (async_state.pending.valid) {
      if (!wait_pending_locked(dev, 60 * 1000000000ll)) {
         pthread_mutex_unlock(&async_state.lock);
         return 0;
      }
   }

   struct base_jd_atom_v2 atoms[2];
   unsigned nr = 0;
   if (vtc_jc) {
      atoms[nr] = (struct base_jd_atom_v2){
         .jc = vtc_jc,
         .atom_number = alloc_atom(),
         .core_req = core_req_for(vtc_reqs),
      };
      nr++;
   }
   if (frag_jc) {
      atoms[nr] = (struct base_jd_atom_v2){
         .jc = frag_jc,
         .core_req = core_req_for(frag_reqs),
      };
      atoms[nr].atom_number = alloc_atom();
      if (nr > 0) {
         atoms[nr].pre_dep[0].atom_id = atoms[0].atom_number;
         atoms[nr].pre_dep[0].dependency_type = BASE_JD_DEP_TYPE_ORDER;
      }
      nr++;
   }

   struct kbase_ioctl_job_submit sub = {
      .addr = (uint64_t)(uintptr_t)&atoms,
      .nr_atoms = nr,
      .stride = atom_stride(),
   };

   int ret = ioctl(dev->kmod.dev->fd, KBASE_IOCTL_JOB_SUBMIT, &sub);
   if (ret) {
      mesa_loge("panfrost/kbase: async JOB_SUBMIT failed: %s", strerror(errno));
      pthread_mutex_unlock(&async_state.lock);
      return 0;
   }

   uint64_t seq = ++async_state.seqno;
   if (seq == 0)
      seq = ++async_state.seqno; /* keep 0 as "nothing submitted" */
   async_state.pending.valid = true;
   async_state.pending.seq = seq;
   memcpy(async_state.pending.atoms, atoms, sizeof(atoms));
   async_state.pending.nr = nr;

   pthread_mutex_unlock(&async_state.lock);
   return seq;
}
