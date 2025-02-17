// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_PIPELINE_LAYOUT_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_PIPELINE_LAYOUT_H_

#include "third_party/blink/renderer/modules/webgpu/dawn_object.h"

namespace blink {

class GPUPipelineLayout : public DawnObject<DawnPipelineLayout> {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static GPUPipelineLayout* Create(GPUDevice* device,
                                   DawnPipelineLayout pipeline_layout);
  explicit GPUPipelineLayout(GPUDevice* device,
                             DawnPipelineLayout pipeline_layout);
  ~GPUPipelineLayout() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(GPUPipelineLayout);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_PIPELINE_LAYOUT_H_
