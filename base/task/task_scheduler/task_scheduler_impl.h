// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_TASK_SCHEDULER_IMPL_H_
#define BASE_TASK_TASK_SCHEDULER_TASK_SCHEDULER_IMPL_H_

#include <memory>
#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/atomic_flag.h"
#include "base/task/single_thread_task_runner_thread_mode.h"
#include "base/task/task_scheduler/delayed_task_manager.h"
#include "base/task/task_scheduler/environment_config.h"
#include "base/task/task_scheduler/scheduler_single_thread_task_runner_manager.h"
#include "base/task/task_scheduler/scheduler_task_runner_delegate.h"
#include "base/task/task_scheduler/scheduler_worker_pool_impl.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "base/task/task_scheduler/task_tracker.h"
#include "base/task/task_traits.h"
#include "base/updateable_sequenced_task_runner.h"
#include "build/build_config.h"

#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
#include "base/task/task_scheduler/task_tracker_posix.h"
#endif

#if defined(OS_WIN)
#include "base/task/task_scheduler/platform_native_worker_pool_win.h"
#include "base/win/com_init_check_hook.h"
#endif

#if defined(OS_MACOSX)
#include "base/task/task_scheduler/platform_native_worker_pool_mac.h"
#endif

namespace base {

class Thread;

namespace internal {

// Default TaskScheduler implementation. This class is thread-safe.
class BASE_EXPORT TaskSchedulerImpl : public TaskScheduler,
                                      public SchedulerWorkerPool::Delegate,
                                      public SchedulerTaskRunnerDelegate {
 public:
  using TaskTrackerImpl =
#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
      TaskTrackerPosix;
#else
      TaskTracker;
#endif

  // Creates a TaskSchedulerImpl with a production TaskTracker.
  //|histogram_label| is used to label histograms, it must not be empty.
  explicit TaskSchedulerImpl(StringPiece histogram_label);

  // For testing only. Creates a TaskSchedulerImpl with a custom TaskTracker.
  TaskSchedulerImpl(StringPiece histogram_label,
                    std::unique_ptr<TaskTrackerImpl> task_tracker);

  ~TaskSchedulerImpl() override;

  // TaskScheduler:
  void Start(const TaskScheduler::InitParams& init_params,
             SchedulerWorkerObserver* scheduler_worker_observer) override;
  int GetMaxConcurrentNonBlockedTasksWithTraitsDeprecated(
      const TaskTraits& traits) const override;
  void Shutdown() override;
  void FlushForTesting() override;
  void FlushAsyncForTesting(OnceClosure flush_callback) override;
  void JoinForTesting() override;
  void SetExecutionFenceEnabled(bool execution_fence_enabled) override;

  // TaskExecutor:
  bool PostDelayedTaskWithTraits(const Location& from_here,
                                 const TaskTraits& traits,
                                 OnceClosure task,
                                 TimeDelta delay) override;
  scoped_refptr<TaskRunner> CreateTaskRunnerWithTraits(
      const TaskTraits& traits) override;
  scoped_refptr<SequencedTaskRunner> CreateSequencedTaskRunnerWithTraits(
      const TaskTraits& traits) override;
  scoped_refptr<SingleThreadTaskRunner> CreateSingleThreadTaskRunnerWithTraits(
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode) override;
#if defined(OS_WIN)
  scoped_refptr<SingleThreadTaskRunner> CreateCOMSTATaskRunnerWithTraits(
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode) override;
#endif  // defined(OS_WIN)
  scoped_refptr<UpdateableSequencedTaskRunner>
  CreateUpdateableSequencedTaskRunnerWithTraitsForTesting(
      const TaskTraits& traits);

 private:
  // Returns |traits|, with priority set to TaskPriority::USER_BLOCKING if
  // |all_tasks_user_blocking_| is set.
  TaskTraits SetUserBlockingPriorityIfNeeded(const TaskTraits& traits) const;

  void ReportHeartbeatMetrics() const;

  // Returns the thread pool responsible for foreground execution.
  const SchedulerWorkerPool* GetForegroundWorkerPool() const;
  SchedulerWorkerPool* GetForegroundWorkerPool();

  const SchedulerWorkerPool* GetWorkerPoolForTraits(
      const TaskTraits& traits) const;

  // SchedulerWorkerPool::Delegate:
  SchedulerWorkerPool* GetWorkerPoolForTraits(
      const TaskTraits& traits) override;

  // SchedulerTaskRunnerDelegate:
  bool PostTaskWithSequence(Task task,
                            scoped_refptr<Sequence> sequence) override;
  bool IsRunningPoolWithTraits(const TaskTraits& traits) const override;
  void UpdatePriority(scoped_refptr<Sequence> sequence,
                      TaskPriority priority) override;

  const std::unique_ptr<TaskTrackerImpl> task_tracker_;
  std::unique_ptr<Thread> service_thread_;
  DelayedTaskManager delayed_task_manager_;
  SchedulerSingleThreadTaskRunnerManager single_thread_task_runner_manager_;

  // Indicates that all tasks are handled as if they had been posted with
  // TaskPriority::USER_BLOCKING. Since this is set in Start(), it doesn't apply
  // to tasks posted before Start() or to tasks posted to TaskRunners created
  // before Start().
  //
  // TODO(fdoray): Remove after experiment. https://crbug.com/757022
  AtomicFlag all_tasks_user_blocking_;

  Optional<SchedulerWorkerPoolImpl> foreground_pool_;
  Optional<SchedulerWorkerPoolImpl> background_pool_;

#if defined(OS_WIN)
  Optional<PlatformNativeWorkerPoolWin> native_foreground_pool_;
#elif defined(OS_MACOSX)
  Optional<PlatformNativeWorkerPoolMac> native_foreground_pool_;
#endif

#if DCHECK_IS_ON()
  // Set once JoinForTesting() has returned.
  AtomicFlag join_for_testing_returned_;
#endif

#if defined(OS_WIN) && defined(COM_INIT_CHECK_HOOK_ENABLED)
  // Provides COM initialization verification for supported builds.
  base::win::ComInitCheckHook com_init_check_hook_;
#endif

  TrackedRefFactory<SchedulerWorkerPool::Delegate> tracked_ref_factory_;

  DISALLOW_COPY_AND_ASSIGN(TaskSchedulerImpl);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_TASK_SCHEDULER_IMPL_H_
