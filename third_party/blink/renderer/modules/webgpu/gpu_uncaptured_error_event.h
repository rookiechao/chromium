// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_UNCAPTURED_ERROR_EVENT_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_UNCAPTURED_ERROR_EVENT_H_

#include "third_party/blink/renderer/modules/event_modules.h"

namespace blink {

class GPUUncapturedErrorEventInit;

class GPUUncapturedErrorEvent : public Event {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static GPUUncapturedErrorEvent* Create(const AtomicString& type,
                                         const GPUUncapturedErrorEventInit*);
  GPUUncapturedErrorEvent(const AtomicString& type,
                          const GPUUncapturedErrorEventInit*);

  // gpu_uncaptured_error_event.idl
  // TODO(crbug.com/877147): implement GPUUncapturedErrorEvent.

 private:
  DISALLOW_COPY_AND_ASSIGN(GPUUncapturedErrorEvent);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_UNCAPTURED_ERROR_EVENT_H_
