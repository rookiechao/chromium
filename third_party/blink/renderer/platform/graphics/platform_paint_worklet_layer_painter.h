// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_PLATFORM_PAINT_WORKLET_LAYER_PAINTER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_PLATFORM_PAINT_WORKLET_LAYER_PAINTER_H_

#include <memory>

#include "cc/paint/paint_record.h"
#include "cc/paint/paint_worklet_layer_painter.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

class PaintWorkletPaintDispatcher;

// This class serves as a bridge which connects the compositor and the paint
// worklet thread. The compositor issues requests to execute the JS paint
// callback, and this class asks the PaintWorkletPaintDispatcher to dispatch the
// request to the paint worklet thread.
class PLATFORM_EXPORT PlatformPaintWorkletLayerPainter
    : public cc::PaintWorkletLayerPainter {
 public:
  explicit PlatformPaintWorkletLayerPainter(
      scoped_refptr<PaintWorkletPaintDispatcher>);
  ~PlatformPaintWorkletLayerPainter() override;

  // cc::PaintWorkletLayerPainter
  sk_sp<cc::PaintRecord> Paint(cc::PaintWorkletInput*) override;

 private:
  scoped_refptr<PaintWorkletPaintDispatcher> dispatcher_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_PLATFORM_PAINT_WORKLET_LAYER_PAINTER_H_
