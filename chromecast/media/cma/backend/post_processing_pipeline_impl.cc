// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/media/cma/backend/post_processing_pipeline_impl.h"

#include <cmath>
#include <string>

#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/scoped_native_library.h"
#include "base/values.h"
#include "chromecast/public/media/audio_post_processor2_shlib.h"
#include "chromecast/public/volume_control.h"

namespace chromecast {
namespace media {

namespace {

// Used for AudioPostProcessor(1)
const char kJsonKeyProcessor[] = "processor";

// Used for AudioPostProcessor2
const char kJsonKeyLib[] = "lib";

const char kJsonKeyName[] = "name";
const char kJsonKeyConfig[] = "config";

}  // namespace

PostProcessingPipelineFactoryImpl::PostProcessingPipelineFactoryImpl() =
    default;
PostProcessingPipelineFactoryImpl::~PostProcessingPipelineFactoryImpl() =
    default;

std::unique_ptr<PostProcessingPipeline>
PostProcessingPipelineFactoryImpl::CreatePipeline(
    const std::string& name,
    const base::Value* filter_description_list,
    int num_channels) {
  return std::make_unique<PostProcessingPipelineImpl>(
      name, filter_description_list, num_channels);
}

PostProcessingPipelineImpl::PostProcessingPipelineImpl(
    const std::string& name,
    const base::Value* filter_description_list,
    int channels)
    : name_(name), num_output_channels_(channels) {
  if (!filter_description_list) {
    return;  // Warning logged.
  }
  for (const base::Value& processor_description_dict :
       filter_description_list->GetList()) {
    DCHECK(processor_description_dict.is_dict());

    std::string processor_name;
    const base::Value* name_val = processor_description_dict.FindKeyOfType(
        kJsonKeyName, base::Value::Type::STRING);
    if (name_val) {
      processor_name = name_val->GetString();
    }

    if (!processor_name.empty()) {
      std::vector<PostProcessorInfo>::iterator it =
          find_if(processors_.begin(), processors_.end(),
                  [&processor_name](PostProcessorInfo& p) {
                    return p.name == processor_name;
                  });
      LOG_IF(DFATAL, it != processors_.end())
          << "Duplicate postprocessor name " << processor_name;
    }

    std::string library_path;

    // Keys for AudioPostProcessor2:
    const base::Value* library_val = processor_description_dict.FindKeyOfType(
        kJsonKeyLib, base::Value::Type::STRING);
    if (library_val) {
      library_path = library_val->GetString();
    } else {
      // Keys for AudioPostProcessor
      // TODO(bshaya): Remove when AudioPostProcessor support is removed.
      library_val = processor_description_dict.FindKeyOfType(
          kJsonKeyProcessor, base::Value::Type::STRING);
      DCHECK(library_val) << "Post processor description is missing key "
                          << kJsonKeyLib;
      library_path = library_val->GetString();
    }

    std::string processor_config_string;
    const base::Value* processor_config_val =
        processor_description_dict.FindKey(kJsonKeyConfig);
    if (processor_config_val) {
      DCHECK(processor_config_val->is_dict() ||
             processor_config_val->is_string());
      base::JSONWriter::Write(*processor_config_val, &processor_config_string);
    }

    LOG(INFO) << "Creating an instance of " << library_path << "("
              << processor_config_string << ")";

    processors_.emplace_back(
        PostProcessorInfo{factory_.CreatePostProcessor(
                              library_path, processor_config_string, channels),
                          processor_name});
    channels = processors_.back().ptr->GetStatus().output_channels;
  }
  num_output_channels_ = channels;
}

PostProcessingPipelineImpl::~PostProcessingPipelineImpl() = default;

double PostProcessingPipelineImpl::ProcessFrames(float* data,
                                                 int num_frames,
                                                 float current_multiplier,
                                                 bool is_silence) {
  DCHECK_GT(input_sample_rate_, 0);
  DCHECK(data);

  output_buffer_ = data;

  if (is_silence) {
    if (!IsRinging()) {
      return delay_s_;  // Output will be silence.
    }
    silence_frames_processed_ += num_frames;
  } else {
    silence_frames_processed_ = 0;
  }

  UpdateCastVolume(current_multiplier);

  delay_s_ = 0;
  for (auto& processor : processors_) {
    processor.ptr->ProcessFrames(output_buffer_, num_frames, cast_volume_,
                                 current_dbfs_);
    const auto& status = processor.ptr->GetStatus();
    delay_s_ += static_cast<double>(status.rendering_delay_frames) /
                status.input_sample_rate;
    output_buffer_ = status.output_buffer;
  }
  return delay_s_;
}

int PostProcessingPipelineImpl::NumOutputChannels() {
  return num_output_channels_;
}

float* PostProcessingPipelineImpl::GetOutputBuffer() {
  DCHECK(output_buffer_);

  return output_buffer_;
}

bool PostProcessingPipelineImpl::SetOutputSampleRate(int sample_rate) {
  output_sample_rate_ = sample_rate;
  input_sample_rate_ = sample_rate;

  // Each Processor's output rate must be the following processor's input rate.
  for (int i = processors_.size() - 1; i >= 0; --i) {
    AudioPostProcessor2::Config config;
    config.output_sample_rate = input_sample_rate_;
    if (!processors_[i].ptr->SetConfig(config)) {
      return false;
    }
    input_sample_rate_ = processors_[i].ptr->GetStatus().input_sample_rate;
  }
  ringing_time_in_frames_ = GetRingingTimeInFrames();
  silence_frames_processed_ = 0;
  return true;
}

int PostProcessingPipelineImpl::GetInputSampleRate() {
  return input_sample_rate_;
}

bool PostProcessingPipelineImpl::IsRinging() {
  return ringing_time_in_frames_ < 0 ||
         silence_frames_processed_ < ringing_time_in_frames_;
}

int PostProcessingPipelineImpl::GetRingingTimeInFrames() {
  int memory_frames = 0;
  for (auto& processor : processors_) {
    int ringing_time = processor.ptr->GetStatus().ringing_time_frames;
    if (ringing_time < 0) {
      return -1;
    }
    memory_frames += ringing_time;
  }
  return memory_frames;
}

void PostProcessingPipelineImpl::UpdateCastVolume(float multiplier) {
  DCHECK_GE(multiplier, 0.0);

  if (multiplier == current_multiplier_) {
    return;
  }
  current_multiplier_ = multiplier;
  current_dbfs_ = (multiplier == 0.0f ? -200.0f : std::log10(multiplier) * 20);
  DCHECK(chromecast::media::VolumeControl::DbFSToVolume);
  cast_volume_ = chromecast::media::VolumeControl::DbFSToVolume(current_dbfs_);
}

// Send string |config| to postprocessor |name|.
void PostProcessingPipelineImpl::SetPostProcessorConfig(
    const std::string& name,
    const std::string& config) {
  DCHECK(!name.empty());
  std::vector<PostProcessorInfo>::iterator it =
      find_if(processors_.begin(), processors_.end(),
              [&name](PostProcessorInfo& p) { return p.name == name; });
  if (it != processors_.end()) {
    it->ptr->UpdateParameters(config);
    LOG(INFO) << "Config string: " << config
              << " was delivered to postprocessor " << name;
  }
}

// Set content type.
void PostProcessingPipelineImpl::SetContentType(AudioContentType content_type) {
  for (auto& processor : processors_) {
    processor.ptr->SetContentType(content_type);
  }
}

void PostProcessingPipelineImpl::UpdatePlayoutChannel(int channel) {
  for (auto& processor : processors_) {
    processor.ptr->SetPlayoutChannel(channel);
  }
}

}  // namespace media
}  // namespace chromecast
