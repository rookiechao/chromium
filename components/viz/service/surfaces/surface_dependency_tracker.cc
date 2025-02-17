// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/surfaces/surface_dependency_tracker.h"

#include "build/build_config.h"
#include "components/viz/common/surfaces/surface_info.h"
#include "components/viz/service/surfaces/surface.h"
#include "components/viz/service/surfaces/surface_manager.h"

namespace viz {

SurfaceDependencyTracker::SurfaceDependencyTracker(
    SurfaceManager* surface_manager)
    : surface_manager_(surface_manager) {}

SurfaceDependencyTracker::~SurfaceDependencyTracker() = default;

void SurfaceDependencyTracker::TrackEmbedding(Surface* surface) {
  // If |surface| is blocking on the arrival of a parent and the parent frame
  // has not yet arrived then track this |surface|'s SurfaceId by FrameSinkId so
  // that if a parent refers to it or a more recent surface, then
  // SurfaceDependencyTracker reports back that a dependency has been added.
  if (surface->block_activation_on_parent() && !surface->HasDependentFrame()) {
    surfaces_blocked_on_parent_by_frame_sink_id_[surface->surface_id()
                                                     .frame_sink_id()]
        .insert(surface->surface_id());
  }
}

void SurfaceDependencyTracker::RequestSurfaceResolution(Surface* surface) {
  DCHECK(surface->HasPendingFrame());

  // Activation dependencies that aren't currently known to the surface manager
  // or do not have an active CompositorFrame block this frame.
  for (const SurfaceId& surface_id : surface->activation_dependencies()) {
    Surface* dependency = surface_manager_->GetSurfaceForId(surface_id);
    if (!dependency || !dependency->HasActiveFrame()) {
      blocked_surfaces_from_dependency_[surface_id.frame_sink_id()].insert(
          surface->surface_id());
    }
  }
}

bool SurfaceDependencyTracker::HasSurfaceBlockedOn(
    const FrameSinkId& frame_sink_id) const {
  auto it = blocked_surfaces_from_dependency_.find(frame_sink_id);
  DCHECK(it == blocked_surfaces_from_dependency_.end() || !it->second.empty());
  return it != blocked_surfaces_from_dependency_.end();
}

void SurfaceDependencyTracker::OnSurfaceActivated(Surface* surface) {
  NotifySurfaceIdAvailable(surface->surface_id());
  // We treat an activation (by deadline) as being the equivalent of a parent
  // embedding the surface.
  OnSurfaceDependencyAdded(surface->surface_id());
}

void SurfaceDependencyTracker::OnSurfaceDependencyAdded(
    const SurfaceId& surface_id) {
  auto it = surfaces_blocked_on_parent_by_frame_sink_id_.find(
      surface_id.frame_sink_id());
  if (it == surfaces_blocked_on_parent_by_frame_sink_id_.end())
    return;

  std::vector<SurfaceId> dependencies_to_notify;

  base::flat_set<SurfaceId>& blocked_surfaces = it->second;
  for (auto iter = blocked_surfaces.begin(); iter != blocked_surfaces.end();) {
    bool should_notify =
        iter->local_surface_id() <= surface_id.local_surface_id();
#if defined(OS_ANDROID)
    // On Android we work around a throttling bug by also firing if the
    // immediately preceding surface has a dependency added.
    // TODO(https://crbug.com/898460): Solve this generally.
    bool is_same_parent =
        iter->local_surface_id().parent_sequence_number() ==
        surface_id.local_surface_id().parent_sequence_number();
    bool is_next_child =
        iter->local_surface_id().child_sequence_number() ==
        surface_id.local_surface_id().child_sequence_number() + 1;
    should_notify |= is_same_parent && is_next_child;
#endif
    if (should_notify) {
      dependencies_to_notify.push_back(*iter);
      iter = blocked_surfaces.erase(iter);
    } else {
      ++iter;
    }
  }

  if (blocked_surfaces.empty())
    surfaces_blocked_on_parent_by_frame_sink_id_.erase(it);

  for (const SurfaceId& dependency : dependencies_to_notify) {
    Surface* surface = surface_manager_->GetSurfaceForId(dependency);
    if (surface)
      surface->OnSurfaceDependencyAdded();
  }
}

void SurfaceDependencyTracker::OnSurfaceDependenciesChanged(
    Surface* surface,
    const base::flat_set<FrameSinkId>& added_dependencies,
    const base::flat_set<FrameSinkId>& removed_dependencies) {
  // Update the |blocked_surfaces_from_dependency_| map with the changes in
  // dependencies.
  for (const FrameSinkId& frame_sink_id : added_dependencies) {
    blocked_surfaces_from_dependency_[frame_sink_id].insert(
        surface->surface_id());
  }

  for (const FrameSinkId& frame_sink_id : removed_dependencies) {
    auto it = blocked_surfaces_from_dependency_.find(frame_sink_id);
    if (it != blocked_surfaces_from_dependency_.end()) {
      it->second.erase(surface->surface_id());
      if (it->second.empty())
        blocked_surfaces_from_dependency_.erase(it);
    }
  }
}

void SurfaceDependencyTracker::OnSurfaceDiscarded(Surface* surface) {
  base::flat_set<FrameSinkId> removed_dependencies;
  for (const SurfaceId& surface_id : surface->activation_dependencies())
    removed_dependencies.insert(surface_id.frame_sink_id());

  OnSurfaceDependenciesChanged(surface, {}, removed_dependencies);

  // Pretend that the discarded surface's SurfaceId is now available to
  // unblock dependencies because we now know the surface will never activate.
  NotifySurfaceIdAvailable(surface->surface_id());
  OnSurfaceDependencyAdded(surface->surface_id());
}

void SurfaceDependencyTracker::OnFrameSinkInvalidated(
    const FrameSinkId& frame_sink_id) {
  // We now know the frame sink will never generated any more frames,
  // thus unblock all dependencies to any future surfaces.
  NotifySurfaceIdAvailable(SurfaceId::MaxSequenceId(frame_sink_id));
  OnSurfaceDependencyAdded(SurfaceId::MaxSequenceId(frame_sink_id));
}

void SurfaceDependencyTracker::NotifySurfaceIdAvailable(
    const SurfaceId& surface_id) {
  auto it = blocked_surfaces_from_dependency_.find(surface_id.frame_sink_id());
  if (it == blocked_surfaces_from_dependency_.end())
    return;

  // Unblock surfaces that depend on this |surface_id|.
  base::flat_set<SurfaceId> blocked_surfaces_by_id(it->second);

  // Tell each surface about the availability of its blocker.
  for (const SurfaceId& blocked_surface_by_id : blocked_surfaces_by_id) {
    Surface* blocked_surface =
        surface_manager_->GetSurfaceForId(blocked_surface_by_id);
    if (!blocked_surface) {
      // A blocked surface may have been garbage collected during dependency
      // resolution.
      continue;
    }
    blocked_surface->NotifySurfaceIdAvailable(surface_id);
  }
}

}  // namespace viz
