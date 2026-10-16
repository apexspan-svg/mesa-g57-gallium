/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates.
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 */

#include "pan_fence.h"
#include "pan_context.h"
#include "pan_screen.h"

#include <unistd.h>

#include "util/os_time.h"
#include "util/u_inlines.h"
#ifdef HAVE_PAN_KMOD_KBASE
#include "pan_kbase_jm.h"
#endif

void
panfrost_fence_reference(struct pipe_screen *pscreen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence)
{
   struct panfrost_device *dev = pan_device(pscreen);
   struct pipe_fence_handle *old = *ptr;

   if (pipe_reference(&old->reference, &fence->reference)) {
      if (old->syncobj)
         drmSyncobjDestroy(panfrost_device_fd(dev), old->syncobj);
      free(old);
   }

   *ptr = fence;
}

bool
panfrost_fence_finish(struct pipe_screen *pscreen, struct pipe_context *ctx,
                      struct pipe_fence_handle *fence, uint64_t timeout)
{
   struct panfrost_device *dev = pan_device(pscreen);
   int ret;

   if (fence->signaled)
      return true;

#ifdef HAVE_PAN_KMOD_KBASE
   /* kbase: without async, submits are synchronous, so by the time a fence
    * exists the GPU work is already complete. With async, wait for the
    * tracked submission seq. */
   if (pan_kbase_jm_is_kbase(dev)) {
      if (!fence->kbase_seq)
         return true;
      uint64_t abs_timeout = os_time_get_absolute_timeout(timeout);
      if (abs_timeout == OS_TIMEOUT_INFINITE)
         abs_timeout = INT64_MAX;
      /* Absolute timeout here is on CLOCK_MONOTONIC; the wait takes
       * nanoseconds-since-now, so convert (clamped at zero). */
      int64_t now = os_time_get_nano();
      int64_t rel = abs_timeout == INT64_MAX ? INT64_MAX :
         (abs_timeout > (uint64_t)now ? (int64_t)(abs_timeout - now) : 0);
      fence->signaled =
         pan_kbase_async_wait_seq(dev, fence->kbase_seq, rel);
      return fence->signaled;
   }
#endif

   uint64_t abs_timeout = os_time_get_absolute_timeout(timeout);
   if (abs_timeout == OS_TIMEOUT_INFINITE)
      abs_timeout = INT64_MAX;

   ret = drmSyncobjWait(panfrost_device_fd(dev), &fence->syncobj, 1,
                        abs_timeout, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);

   fence->signaled = (ret >= 0);
   return fence->signaled;
}

int
panfrost_fence_get_fd(struct pipe_screen *screen, struct pipe_fence_handle *f)
{
   struct panfrost_device *dev = pan_device(screen);
   int fd = -1;

#ifdef HAVE_PAN_KMOD_KBASE
   if (pan_kbase_jm_is_kbase(dev))
      return -1;
#endif
   drmSyncobjExportSyncFile(panfrost_device_fd(dev), f->syncobj, &fd);
   return fd;
}

struct pipe_fence_handle *
panfrost_fence_from_fd(struct panfrost_context *ctx, int fd,
                       enum pipe_fd_type type)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   int ret;

   struct pipe_fence_handle *f = calloc(1, sizeof(*f));
   if (!f)
      return NULL;

#ifdef HAVE_PAN_KMOD_KBASE
   /* No DRM syncobjs on kbase: hand back an already-signaled fence. */
   if (pan_kbase_jm_is_kbase(dev)) {
      pipe_reference_init(&f->reference, 1);
      f->signaled = true;
      if (type == PIPE_FD_TYPE_NATIVE_SYNC && fd >= 0)
         close(fd);
      return f;
   }
#endif
   if (type == PIPE_FD_TYPE_NATIVE_SYNC) {
      ret = drmSyncobjCreate(panfrost_device_fd(dev), 0, &f->syncobj);
      if (ret) {
         mesa_loge("create syncobj failed\n");
         goto err_free_fence;
      }

      ret = drmSyncobjImportSyncFile(panfrost_device_fd(dev), f->syncobj, fd);
      if (ret) {
         mesa_loge("import syncfile failed\n");
         goto err_destroy_syncobj;
      }
   } else {
      assert(type == PIPE_FD_TYPE_SYNCOBJ);
      ret = drmSyncobjFDToHandle(panfrost_device_fd(dev), fd, &f->syncobj);
      if (ret) {
         mesa_loge("import syncobj FD failed\n");
         goto err_free_fence;
      }
   }

   pipe_reference_init(&f->reference, 1);

   return f;

err_destroy_syncobj:
   drmSyncobjDestroy(panfrost_device_fd(dev), f->syncobj);
err_free_fence:
   free(f);
   return NULL;
}

struct pipe_fence_handle *
panfrost_fence_create(struct panfrost_context *ctx)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   int fd = -1, ret;

   /* Snapshot the last rendering out fence. We'd rather have another
    * syncobj instead of a sync file, but this is all we get.
    * (HandleToFD/FDToHandle just gives you another syncobj ID for the
    * same syncobj).
    */
#ifdef HAVE_PAN_KMOD_KBASE
   if (pan_kbase_jm_is_kbase(dev)) {
      struct pipe_fence_handle *kf = calloc(1, sizeof(*kf));
      if (!kf)
         return NULL;
      pipe_reference_init(&kf->reference, 1);
      /* Synchronous submits are complete by now; async ones track the
       * last submission seq (seq_done(0) is true: nothing outstanding). */
      kf->kbase_seq = pan_kbase_async_enabled() ?
         pan_kbase_async_last_seq() : 0;
      kf->signaled = pan_kbase_async_seq_done(kf->kbase_seq);
      return kf;
   }
#endif
   ret = drmSyncobjExportSyncFile(panfrost_device_fd(dev), ctx->syncobj, &fd);
   if (ret || fd == -1) {
      mesa_loge("export failed\n");
      return NULL;
   }

   struct pipe_fence_handle *f =
      panfrost_fence_from_fd(ctx, fd, PIPE_FD_TYPE_NATIVE_SYNC);

   close(fd);

   return f;
}
