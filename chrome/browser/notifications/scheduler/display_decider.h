// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_SCHEDULER_DISPLAY_DECIDER_H_
#define CHROME_BROWSER_NOTIFICATIONS_SCHEDULER_DISPLAY_DECIDER_H_

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "chrome/browser/notifications/scheduler/internal_types.h"
#include "chrome/browser/notifications/scheduler/notification_scheduler_types.h"

namespace notifications {

class DistributionPolicy;
struct TypeState;
struct NotificationEntry;
struct SchedulerConfig;

// This class uses scheduled notifications data and notification impression data
// of each notification type to find a list of notification that should be
// displayed to the user.
// All operations should be done on the main thread.
class DisplayDecider {
 public:
  using Notifications =
      std::map<SchedulerClientType, std::vector<const NotificationEntry*>>;
  using TypeStates = std::map<SchedulerClientType, const TypeState*>;
  using Results = std::set<std::string>;

  // Creates the decider to determine notifications to show.
  static std::unique_ptr<DisplayDecider> Create();

  DisplayDecider() = default;
  virtual ~DisplayDecider() = default;

  // Finds notifications to show. Returns a list of notification guids.
  virtual void FindNotificationsToShow(
      const SchedulerConfig* config,
      std::vector<SchedulerClientType> clients,
      std::unique_ptr<DistributionPolicy> distribution_policy,
      SchedulerTaskTime task_start_time,
      Notifications notifications,
      TypeStates type_states,
      Results* results) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(DisplayDecider);
};

}  // namespace notifications

#endif  // CHROME_BROWSER_NOTIFICATIONS_SCHEDULER_DISPLAY_DECIDER_H_
