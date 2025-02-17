// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/fragment_data.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/platform/graphics/paint/geometry_mapper.h"

namespace blink {

// These are defined here because of PaintLayer dependency.

FragmentData::RareData::RareData() : unique_id(NewUniqueObjectId()) {}

FragmentData::RareData::~RareData() = default;

void FragmentData::DestroyTail() {
  while (next_fragment_) {
    // Take the following (next-next) fragment, clearing
    // |next_fragment_->next_fragment_|.
    std::unique_ptr<FragmentData> next =
        std::move(next_fragment_->next_fragment_);
    // Point |next_fragment_| to the following fragment and destroy
    // the current |next_fragment_|.
    next_fragment_ = std::move(next);
  }
}

FragmentData& FragmentData::EnsureNextFragment() {
  if (!next_fragment_)
    next_fragment_ = std::make_unique<FragmentData>();
  return *next_fragment_.get();
}

FragmentData::RareData& FragmentData::EnsureRareData() {
  if (!rare_data_)
    rare_data_ = std::make_unique<RareData>();
  return *rare_data_;
}

void FragmentData::SetLayer(std::unique_ptr<PaintLayer> layer) {
  if (rare_data_ || layer)
    EnsureRareData().layer = std::move(layer);
}

const TransformPaintPropertyNode& FragmentData::PreTransform() const {
  if (const auto* properties = PaintProperties()) {
    if (const auto* transform = properties->Transform()) {
      DCHECK(transform->Parent());
      return *transform->Parent();
    }
  }
  return LocalBorderBoxProperties().Transform();
}

const TransformPaintPropertyNode& FragmentData::PostScrollTranslation() const {
  if (const auto* properties = PaintProperties()) {
    if (properties->TransformIsolationNode())
      return *properties->TransformIsolationNode();
    if (properties->ScrollTranslation())
      return *properties->ScrollTranslation();
    if (properties->ReplacedContentTransform())
      return *properties->ReplacedContentTransform();
    if (properties->Perspective())
      return *properties->Perspective();
  }
  return LocalBorderBoxProperties().Transform();
}

const ClipPaintPropertyNode& FragmentData::PreClip() const {
  if (const auto* properties = PaintProperties()) {
    if (const auto* clip = properties->ClipPathClip()) {
      // SPv1 composited clip-path has an alternative clip tree structure.
      // If the clip-path is parented by the mask clip, it is only used
      // to clip mask layer chunks, and not in the clip inheritance chain.
      DCHECK(clip->Parent());
      if (clip->Parent() != properties->MaskClip())
        return *clip->Parent();
    }
    if (const auto* mask_clip = properties->MaskClip()) {
      DCHECK(mask_clip->Parent());
      return *mask_clip->Parent();
    }
    if (const auto* css_clip = properties->CssClip()) {
      DCHECK(css_clip->Parent());
      return *css_clip->Parent();
    }
  }
  return LocalBorderBoxProperties().Clip();
}

const ClipPaintPropertyNode& FragmentData::PostOverflowClip() const {
  if (const auto* properties = PaintProperties()) {
    if (properties->ClipIsolationNode())
      return *properties->ClipIsolationNode();
    if (properties->OverflowClip())
      return *properties->OverflowClip();
    if (properties->InnerBorderRadiusClip())
      return *properties->InnerBorderRadiusClip();
  }
  return LocalBorderBoxProperties().Clip();
}

const EffectPaintPropertyNode& FragmentData::PreEffect() const {
  if (const auto* properties = PaintProperties()) {
    if (const auto* effect = properties->Effect()) {
      DCHECK(effect->Parent());
      return *effect->Parent();
    }
    if (const auto* filter = properties->Filter()) {
      DCHECK(filter->Parent());
      return *filter->Parent();
    }
  }
  return LocalBorderBoxProperties().Effect();
}

const EffectPaintPropertyNode& FragmentData::PreFilter() const {
  if (const auto* properties = PaintProperties()) {
    if (const auto* filter = properties->Filter()) {
      DCHECK(filter->Parent());
      return *filter->Parent();
    }
  }
  return LocalBorderBoxProperties().Effect();
}

const EffectPaintPropertyNode& FragmentData::PostIsolationEffect() const {
  if (const auto* properties = PaintProperties()) {
    if (properties->EffectIsolationNode())
      return *properties->EffectIsolationNode();
  }
  return LocalBorderBoxProperties().Effect();
}

void FragmentData::InvalidateClipPathCache() {
  if (!rare_data_)
    return;

  rare_data_->is_clip_path_cache_valid = false;
  rare_data_->clip_path_bounding_box = base::nullopt;
  rare_data_->clip_path_path = nullptr;
}

void FragmentData::SetClipPathCache(const IntRect& bounding_box,
                                    scoped_refptr<const RefCountedPath> path) {
  EnsureRareData().is_clip_path_cache_valid = true;
  rare_data_->clip_path_bounding_box = bounding_box;
  rare_data_->clip_path_path = std::move(path);
}

template <typename Rect, typename PaintOffsetFunction>
static void MapRectBetweenFragment(
    const FragmentData& from_fragment,
    const FragmentData& to_fragment,
    const PaintOffsetFunction& paint_offset_function,
    Rect& rect) {
  if (&from_fragment == &to_fragment)
    return;
  const auto& from_transform =
      from_fragment.LocalBorderBoxProperties().Transform();
  const auto& to_transform = to_fragment.LocalBorderBoxProperties().Transform();
  rect.MoveBy(paint_offset_function(from_fragment.PaintOffset()));
  GeometryMapper::SourceToDestinationRect(from_transform, to_transform, rect);
  rect.MoveBy(-paint_offset_function(to_fragment.PaintOffset()));
}

void FragmentData::MapRectToFragment(const FragmentData& fragment,
                                     IntRect& rect) const {
  MapRectBetweenFragment(*this, fragment,
                         [](const LayoutPoint& paint_offset) {
                           return RoundedIntPoint(paint_offset);
                         },
                         rect);
}

void FragmentData::MapRectToFragment(const FragmentData& fragment,
                                     LayoutRect& rect) const {
  MapRectBetweenFragment(
      *this, fragment,
      [](const LayoutPoint& paint_offset) { return paint_offset; }, rect);
}

}  // namespace blink
