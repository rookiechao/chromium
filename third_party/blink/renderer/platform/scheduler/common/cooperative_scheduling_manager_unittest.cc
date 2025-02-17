// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/scheduler/public/cooperative_scheduling_manager.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/testing/wtf/scoped_mock_clock.h"

namespace blink {
namespace scheduler {

TEST(CooperativeSchedulingManager, WhitelistedStackScope) {
  std::unique_ptr<CooperativeSchedulingManager> manager =
      std::make_unique<CooperativeSchedulingManager>();
  {
    EXPECT_FALSE(manager->InWhitelistedStackScope());
    CooperativeSchedulingManager::WhitelistedStackScope scope(manager.get());
    EXPECT_TRUE(manager->InWhitelistedStackScope());
    {
      CooperativeSchedulingManager::WhitelistedStackScope nested_scope(
          manager.get());
      EXPECT_TRUE(manager->InWhitelistedStackScope());
    }
    EXPECT_TRUE(manager->InWhitelistedStackScope());
  }
  EXPECT_FALSE(manager->InWhitelistedStackScope());
}

class MockCooperativeSchedulingManager : public CooperativeSchedulingManager {
 public:
  MockCooperativeSchedulingManager() : CooperativeSchedulingManager() {
    ON_CALL(*this, RunNestedLoop())
        .WillByDefault(testing::Invoke(
            this, &MockCooperativeSchedulingManager::RealRunNestedLoop));
  }
  ~MockCooperativeSchedulingManager() override = default;
  MOCK_METHOD0(RunNestedLoop, void());
  void RealRunNestedLoop() { CooperativeSchedulingManager::RunNestedLoop(); }
};

TEST(CooperativeSchedulingManager, SafePoint) {
  {
    std::unique_ptr<MockCooperativeSchedulingManager> manager =
        std::make_unique<MockCooperativeSchedulingManager>();
    EXPECT_CALL(*manager, RunNestedLoop()).Times(0);
    // Should not run nested loop because stack is not whitelisted
    manager->Safepoint();
  }
  {
    WTF::ScopedMockClock clock;
    std::unique_ptr<MockCooperativeSchedulingManager> manager =
        std::make_unique<MockCooperativeSchedulingManager>();
    CooperativeSchedulingManager::WhitelistedStackScope scope(manager.get());
    EXPECT_CALL(*manager, RunNestedLoop()).Times(2);
    // Should run nested loop
    manager->Safepoint();
    clock.Advance(TimeDelta::FromMilliseconds(14));
    // Should not run nested loop because called too soon
    manager->Safepoint();
    clock.Advance(TimeDelta::FromMilliseconds(2));
    // Should run nested loop
    manager->Safepoint();
  }
}

}  // namespace scheduler
}  // namespace blink
