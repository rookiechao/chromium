// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/hit_test/hit_test_aggregator.h"

#include "components/viz/common/hit_test/aggregated_hit_test_region.h"
#include "components/viz/service/surfaces/latest_local_surface_id_lookup_delegate.h"

namespace viz {

namespace {
// TODO(gklassen): Review and select appropriate sizes based on
// telemetry / UMA.
constexpr uint32_t kMaxRegionsPerSurface = 1024;
}  // namespace

HitTestManager::HitTestAsyncQueriedDebugRegion::
    HitTestAsyncQueriedDebugRegion() = default;
HitTestManager::HitTestAsyncQueriedDebugRegion::HitTestAsyncQueriedDebugRegion(
    base::flat_set<FrameSinkId> regions)
    : regions(std::move(regions)) {}
HitTestManager::HitTestAsyncQueriedDebugRegion::
    ~HitTestAsyncQueriedDebugRegion() = default;

HitTestManager::HitTestAsyncQueriedDebugRegion::HitTestAsyncQueriedDebugRegion(
    HitTestAsyncQueriedDebugRegion&&) = default;
HitTestManager::HitTestAsyncQueriedDebugRegion&
HitTestManager::HitTestAsyncQueriedDebugRegion::operator=(
    HitTestAsyncQueriedDebugRegion&&) = default;

HitTestManager::HitTestManager(SurfaceManager* surface_manager)
    : surface_manager_(surface_manager) {}

HitTestManager::~HitTestManager() = default;

bool HitTestManager::OnSurfaceDamaged(const SurfaceId& surface_id,
                                      const BeginFrameAck& ack) {
  return false;
}

void HitTestManager::OnSurfaceDestroyed(const SurfaceId& surface_id) {
  hit_test_region_lists_.erase(surface_id);
}

void HitTestManager::OnSurfaceActivated(
    const SurfaceId& surface_id,
    base::Optional<base::TimeDelta> duration) {
  // When a Surface is activated we can confidently remove all
  // associated HitTestRegionList objects with an older frame_index.
  auto search = hit_test_region_lists_.find(surface_id);
  if (search == hit_test_region_lists_.end())
    return;

  Surface* surface = surface_manager_->GetSurfaceForId(surface_id);
  DCHECK(surface);
  uint64_t frame_index = surface->GetActiveFrameIndex();

  auto& frame_index_map = search->second;
  for (auto it = frame_index_map.begin(); it != frame_index_map.end();) {
    if (it->first != frame_index)
      it = frame_index_map.erase(it);
    else
      ++it;
  }
}

void HitTestManager::SubmitHitTestRegionList(
    const SurfaceId& surface_id,
    const uint64_t frame_index,
    base::Optional<HitTestRegionList> hit_test_region_list) {
  if (!hit_test_region_list) {
    auto& frame_index_map = hit_test_region_lists_[surface_id];
    if (!frame_index_map.empty()) {
      // We will reuse the last submitted hit-test data.
      uint64_t last_frame_index = frame_index_map.rbegin()->first;

      HitTestRegionList last_hit_test_region_list =
          std::move(frame_index_map[last_frame_index]);

      frame_index_map[frame_index] = std::move(last_hit_test_region_list);
      frame_index_map.erase(last_frame_index);
    }
    return;
  }
  if (!ValidateHitTestRegionList(surface_id, &*hit_test_region_list))
    return;
  ++submit_hit_test_region_list_index_;

  // TODO(gklassen): Runtime validation that hit_test_region_list is valid.
  // TODO(gklassen): Inform FrameSink that the hit_test_region_list is invalid.
  // TODO(gklassen): FrameSink needs to inform the host of a difficult renderer.
  hit_test_region_lists_[surface_id][frame_index] =
      std::move(*hit_test_region_list);
}

const HitTestRegionList* HitTestManager::GetActiveHitTestRegionList(
    LatestLocalSurfaceIdLookupDelegate* delegate,
    const FrameSinkId& frame_sink_id,
    uint64_t* store_active_frame_index) const {
  if (!delegate)
    return nullptr;

  LocalSurfaceId local_surface_id =
      delegate->GetSurfaceAtAggregation(frame_sink_id);
  if (!local_surface_id.is_valid())
    return nullptr;

  SurfaceId surface_id(frame_sink_id, local_surface_id);
  auto search = hit_test_region_lists_.find(surface_id);
  if (search == hit_test_region_lists_.end())
    return nullptr;

  Surface* surface = surface_manager_->GetSurfaceForId(surface_id);
  DCHECK(surface);
  uint64_t frame_index = surface->GetActiveFrameIndex();
  if (store_active_frame_index)
    *store_active_frame_index = frame_index;

  auto& frame_index_map = search->second;
  auto search2 = frame_index_map.find(frame_index);
  if (search2 == frame_index_map.end())
    return nullptr;

  return &search2->second;
}

int64_t HitTestManager::GetTraceId(const SurfaceId& id) const {
  Surface* surface = surface_manager_->GetSurfaceForId(id);
  return surface->GetActiveFrame().metadata.begin_frame_ack.trace_id;
}

const base::flat_set<FrameSinkId>*
HitTestManager::GetHitTestAsyncQueriedDebugRegions(
    const FrameSinkId& root_frame_sink_id) const {
  auto it = hit_test_async_queried_debug_regions_.find(root_frame_sink_id);
  if (it == hit_test_async_queried_debug_regions_.end() ||
      it->second.timer.Elapsed().InMilliseconds() > 2000) {
    return nullptr;
  }
  return &it->second.regions;
}

void HitTestManager::SetHitTestAsyncQueriedDebugRegions(
    const FrameSinkId& root_frame_sink_id,
    const std::vector<FrameSinkId>& hit_test_async_queried_debug_queue) {
  hit_test_async_queried_debug_regions_[root_frame_sink_id] =
      HitTestAsyncQueriedDebugRegion(base::flat_set<FrameSinkId>(
          hit_test_async_queried_debug_queue.begin(),
          hit_test_async_queried_debug_queue.end()));
}

bool HitTestManager::ValidateHitTestRegionList(
    const SurfaceId& surface_id,
    HitTestRegionList* hit_test_region_list) {
  if (hit_test_region_list->regions.size() > kMaxRegionsPerSurface)
    return false;
  for (auto& region : hit_test_region_list->regions) {
    // TODO(gklassen): Ensure that |region->frame_sink_id| is a child of
    // |frame_sink_id|.
    if (region.frame_sink_id.client_id() == 0) {
      region.frame_sink_id = FrameSinkId(surface_id.frame_sink_id().client_id(),
                                         region.frame_sink_id.sink_id());
    }
  }
  return true;
}

}  // namespace viz
