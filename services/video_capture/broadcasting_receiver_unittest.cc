// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/video_capture/broadcasting_receiver.h"

#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "media/capture/video/shared_memory_handle_provider.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/video_capture/public/cpp/mock_receiver.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::InvokeWithoutArgs;

namespace video_capture {

class FakeAccessPermission : public mojom::ScopedAccessPermission {
 public:
  FakeAccessPermission(base::OnceClosure destruction_cb)
      : destruction_cb_(std::move(destruction_cb)) {}
  ~FakeAccessPermission() override { std::move(destruction_cb_).Run(); }

 private:
  base::OnceClosure destruction_cb_;
};

class BroadcastingReceiverTest : public ::testing::Test {
 public:
  void SetUp() override {
    mojom::ReceiverPtr receiver_1;
    mojom::ReceiverPtr receiver_2;
    mock_receiver_1_ =
        std::make_unique<MockReceiver>(mojo::MakeRequest(&receiver_1));
    mock_receiver_2_ =
        std::make_unique<MockReceiver>(mojo::MakeRequest(&receiver_2));
    broadcaster_.AddClient(std::move(receiver_1),
                           media::VideoCaptureBufferType::kSharedMemory);
    broadcaster_.AddClient(std::move(receiver_2),
                           media::VideoCaptureBufferType::kSharedMemory);
  }

 protected:
  BroadcastingReceiver broadcaster_;
  std::unique_ptr<MockReceiver> mock_receiver_1_;
  std::unique_ptr<MockReceiver> mock_receiver_2_;
  base::test::ScopedTaskEnvironment task_environment_;
};

TEST_F(
    BroadcastingReceiverTest,
    HoldsOnToAccessPermissionForRetiredBufferUntilLastClientFinishedConsuming) {
  media::SharedMemoryHandleProvider shm_provider;
  const size_t kArbitraryDummyBufferSize = 8u;
  ASSERT_TRUE(shm_provider.InitForSize(kArbitraryDummyBufferSize));
  media::mojom::VideoBufferHandlePtr buffer_handle =
      media::mojom::VideoBufferHandle::New();
  buffer_handle->set_shared_buffer_handle(
      shm_provider.GetHandleForInterProcessTransit(true /*read_only*/));
  static const int kArbiraryBufferId = 123;
  static const int kArbiraryFrameFeedbackId = 456;
  broadcaster_.OnNewBuffer(kArbiraryBufferId, std::move(buffer_handle));

  base::RunLoop frame_arrived_at_receiver_1;
  base::RunLoop frame_arrived_at_receiver_2;
  EXPECT_CALL(*mock_receiver_1_, DoOnFrameReadyInBuffer(_, _, _, _))
      .WillOnce(InvokeWithoutArgs([&frame_arrived_at_receiver_1]() {
        frame_arrived_at_receiver_1.Quit();
      }));
  EXPECT_CALL(*mock_receiver_2_, DoOnFrameReadyInBuffer(_, _, _, _))
      .WillOnce(InvokeWithoutArgs([&frame_arrived_at_receiver_2]() {
        frame_arrived_at_receiver_2.Quit();
      }));
  mock_receiver_2_->HoldAccessPermissions();

  mojom::ScopedAccessPermissionPtr access_permission;
  bool access_permission_has_been_released = false;
  mojo::MakeStrongBinding(std::make_unique<FakeAccessPermission>(base::BindOnce(
                              [](bool* access_permission_has_been_released) {
                                *access_permission_has_been_released = true;
                              },
                              &access_permission_has_been_released)),
                          mojo::MakeRequest(&access_permission));
  media::mojom::VideoFrameInfoPtr frame_info =
      media::mojom::VideoFrameInfo::New();
  media::VideoFrameMetadata frame_metadata;
  frame_info->metadata = frame_metadata.GetInternalValues().Clone();
  broadcaster_.OnFrameReadyInBuffer(kArbiraryBufferId, kArbiraryFrameFeedbackId,
                                    std::move(access_permission),
                                    std::move(frame_info));

  // mock_receiver_1_ finishes consuming immediately.
  // mock_receiver_2_ continues consuming.
  frame_arrived_at_receiver_1.Run();
  frame_arrived_at_receiver_2.Run();

  base::RunLoop buffer_retired_arrived_at_receiver_1;
  base::RunLoop buffer_retired_arrived_at_receiver_2;
  EXPECT_CALL(*mock_receiver_1_, DoOnBufferRetired(_))
      .WillOnce(InvokeWithoutArgs([&buffer_retired_arrived_at_receiver_1]() {
        buffer_retired_arrived_at_receiver_1.Quit();
      }));
  EXPECT_CALL(*mock_receiver_2_, DoOnBufferRetired(_))
      .WillOnce(InvokeWithoutArgs([&buffer_retired_arrived_at_receiver_2]() {
        buffer_retired_arrived_at_receiver_2.Quit();
      }));

  // retire the buffer
  broadcaster_.OnBufferRetired(kArbiraryBufferId);

  // expect that both receivers get the retired event
  buffer_retired_arrived_at_receiver_1.Run();
  buffer_retired_arrived_at_receiver_2.Run();

  // expect that |access_permission| is still being held
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(access_permission_has_been_released);

  // mock_receiver_2_ finishes consuming
  mock_receiver_2_->ReleaseAccessPermissions();

  // expect that |access_permission| is released
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(access_permission_has_been_released);
}

}  // namespace video_capture
