// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/native_pixmap_handle.h"

#include <utility>

#include "build/build_config.h"

#if defined(OS_LINUX)
#include <drm_fourcc.h>
#include "base/posix/eintr_wrapper.h"
#endif

#if defined(OS_FUCHSIA)
#include <lib/zx/vmo.h>
#include "base/fuchsia/fuchsia_logging.h"
#endif

namespace gfx {

#if defined(OS_LINUX)
static_assert(NativePixmapPlane::kNoModifier == DRM_FORMAT_MOD_INVALID,
              "gfx::NativePixmapPlane::kNoModifier should be an alias for"
              "DRM_FORMAT_MOD_INVALID");
#endif

NativePixmapPlane::NativePixmapPlane()
    : stride(0), offset(0), size(0), modifier(0) {}

NativePixmapPlane::NativePixmapPlane(int stride,
                                     int offset,
                                     uint64_t size,
#if defined(OS_LINUX)
                                     base::ScopedFD fd,
#elif defined(OS_FUCHSIA)
                                     zx::vmo vmo,
#endif
                                     uint64_t modifier)
    : stride(stride),
      offset(offset),
      size(size),
      modifier(modifier)
#if defined(OS_LINUX)
      ,
      fd(std::move(fd))
#elif defined(OS_FUCHSIA)
      ,
      vmo(std::move(vmo))
#endif
{
}

NativePixmapPlane::NativePixmapPlane(NativePixmapPlane&& other) = default;

NativePixmapPlane::~NativePixmapPlane() = default;

NativePixmapPlane& NativePixmapPlane::operator=(NativePixmapPlane&& other) =
    default;

NativePixmapHandle::NativePixmapHandle() = default;
NativePixmapHandle::NativePixmapHandle(NativePixmapHandle&& other) = default;

NativePixmapHandle::~NativePixmapHandle() = default;

NativePixmapHandle& NativePixmapHandle::operator=(NativePixmapHandle&& other) =
    default;

NativePixmapHandle CloneHandleForIPC(const NativePixmapHandle& handle) {
  NativePixmapHandle clone;
  for (auto& plane : handle.planes) {
#if defined(OS_LINUX)
    DCHECK(plane.fd.is_valid());
    base::ScopedFD fd_dup(HANDLE_EINTR(dup(plane.fd.get())));
    if (!fd_dup.is_valid()) {
      PLOG(ERROR) << "dup";
      return NativePixmapHandle();
    }
    clone.planes.emplace_back(plane.stride, plane.offset, plane.size,
                              std::move(fd_dup), plane.modifier);
#elif defined(OS_FUCHSIA)
    zx::vmo vmo_dup;
    zx_status_t status = plane.vmo.duplicate(ZX_RIGHT_DUPLICATE, &vmo_dup);
    if (status != ZX_OK) {
      ZX_DLOG(ERROR, status) << "zx_handle_duplicate";
      return NativePixmapHandle();
    }
    clone.planes.emplace_back(plane.stride, plane.offset, plane.size,
                              std::move(vmo_dup), plane.modifier);
#else
#error Unsupported OS
#endif
  }
  return clone;
}

}  // namespace gfx
