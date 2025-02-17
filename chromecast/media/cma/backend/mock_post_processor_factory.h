// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_MEDIA_CMA_BACKEND_MOCK_POST_PROCESSOR_FACTORY_H_
#define CHROMECAST_MEDIA_CMA_BACKEND_MOCK_POST_PROCESSOR_FACTORY_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "base/macros.h"
#include "chromecast/media/cma/backend/post_processing_pipeline.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace base {
class Value;
}  // namespace base

namespace chromecast {
namespace media {

class MockPostProcessorFactory;
class MockPostProcessor : public PostProcessingPipeline {
 public:
  MockPostProcessor(MockPostProcessorFactory* factory,
                    const std::string& name,
                    const base::Value* filter_description_list,
                    int channels);
  ~MockPostProcessor() override;
  MOCK_METHOD4(ProcessFrames,
               double(float* data,
                      int num_frames,
                      float current_volume,
                      bool is_silence));
  MOCK_METHOD1(SetContentType, void(AudioContentType));
  bool SetOutputSampleRate(int sample_rate) override {
    sample_rate_ = sample_rate;
    return true;
  }
  int GetInputSampleRate() override { return sample_rate_; }
  bool IsRinging() override { return ringing_; }
  int delay() { return rendering_delay_frames_; }
  std::string name() const { return name_; }
  float* GetOutputBuffer() override { return output_buffer_; }
  int NumOutputChannels() override { return num_output_channels_; }

  MOCK_METHOD2(SetPostProcessorConfig,
               void(const std::string& name, const std::string& config));
  MOCK_METHOD1(UpdatePlayoutChannel, void(int));

 private:
  double DoProcessFrames(float* data,
                         int num_frames,
                         float current_volume,
                         bool is_silence) {
    output_buffer_ = data;
    return static_cast<double>(rendering_delay_frames_) / sample_rate_;
    ;
  }

  MockPostProcessorFactory* const factory_;
  const std::string name_;
  int sample_rate_;
  int rendering_delay_frames_ = 0;
  bool ringing_ = false;
  float* output_buffer_ = nullptr;
  int num_output_channels_;

  DISALLOW_COPY_AND_ASSIGN(MockPostProcessor);
};

class MockPostProcessorFactory : public PostProcessingPipelineFactory {
 public:
  MockPostProcessorFactory();
  ~MockPostProcessorFactory() override;
  std::unique_ptr<PostProcessingPipeline> CreatePipeline(
      const std::string& name,
      const base::Value* filter_description_list,
      int channels) override;

  std::unordered_map<std::string, MockPostProcessor*> instances;
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_MEDIA_CMA_BACKEND_MOCK_POST_PROCESSOR_FACTORY_H_
