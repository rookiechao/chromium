// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/observers/working_set_trimmer_win.h"

#include <windows.h>  // Must be in front of other Windows header files.

#include <psapi.h>

#include <cstring>
#include <vector>

#include "base/command_line.h"
#include "base/process/process_handle.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/multiprocess_test.h"
#include "chrome/browser/performance_manager/graph/graph_test_harness.h"
#include "chrome/browser/performance_manager/graph/process_node_impl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

namespace performance_manager {

namespace {

constexpr char kTestProcessIdSwitchName[] = "test_test_process_id";
constexpr base::char16 kBufferInitializedEventName[] =
    L"RCEmptyWorkingSetTestBufferInitialized";
constexpr base::char16 kChildProcessExitEventName[] =
    L"RCEmptyWorkingSetTestChildProcessExit";

base::win::ScopedHandle CreateEvent(const base::string16& name,
                                    const base::string16& test_process_id) {
  base::win::ScopedHandle event(::CreateEvent(
      nullptr, TRUE, FALSE, (L"Local\\" + name + test_process_id).c_str()));
  DCHECK(event.IsValid());
  return event;
}

size_t GetWorkingSetSizeMb(base::ProcessHandle handle) {
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (::GetProcessMemoryInfo(handle,
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
    return pmc.WorkingSetSize / 1024 / 1024;
  }

  ADD_FAILURE() << "GetProcessMemoryInfo failed";
  return 0;
}

MULTIPROCESS_TEST_MAIN(ProcessWithLargeWorkingSet) {
  const base::string16 test_process_id =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueNative(
          kTestProcessIdSwitchName);

  constexpr int k10MbInBytes = 10 * 1024 * 1024;
  std::vector<char> buffer(k10MbInBytes);
  std::memset(buffer.data(), 0x80, buffer.size());

  base::WaitableEvent buffer_initialized_event(
      CreateEvent(kBufferInitializedEventName, test_process_id));
  buffer_initialized_event.Signal();

  base::WaitableEvent child_process_exit_event(
      CreateEvent(kChildProcessExitEventName, test_process_id));
  child_process_exit_event.Wait();

  return 0;
}

class WorkingSetTrimmerTest : public GraphTestHarness {
 protected:
  WorkingSetTrimmerTest() {
    // Create a child process and wait until it allocates a 10 MB buffer.
    base::CommandLine command_line(
        base::GetMultiProcessTestChildBaseCommandLine());
    command_line.AppendSwitchNative(kTestProcessIdSwitchName, test_process_id_);

    child_process_ = base::SpawnMultiProcessTestChild(
        "ProcessWithLargeWorkingSet", command_line, base::LaunchOptions());

    base::WaitableEvent buffer_initialized_event(
        CreateEvent(kBufferInitializedEventName, test_process_id_));
    buffer_initialized_event.Wait();

    process_node_->SetPID(child_process_.Pid());

    EXPECT_GE(GetWorkingSetSizeMb(child_process_.Handle()), 10U);
  }

  ~WorkingSetTrimmerTest() override {
    // Wait for the child process to exit.
    base::WaitableEvent child_process_exit_event(
        CreateEvent(kChildProcessExitEventName, test_process_id_));
    child_process_exit_event.Signal();

    child_process_.WaitForExit(nullptr);
  }

  const base::string16 test_process_id_ =
      base::NumberToString16(base::GetCurrentProcId());
  base::Process child_process_;
  TestNodeWrapper<ProcessNodeImpl> process_node_ =
      CreateNode<ProcessNodeImpl>();
  WorkingSetTrimmer working_set_trimmer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WorkingSetTrimmerTest);
};

}  // namespace

TEST_F(WorkingSetTrimmerTest, EmptyWorkingSet) {
  // Set the launch time of the process CU to match |child_process_|.
  process_node_->SetLaunchTime(child_process_.CreationTime());

  // When all frames in the Process CU are frozen, the working set of
  // |child_process_| should be emptied.
  size_t working_set_before = GetWorkingSetSizeMb(child_process_.Handle());
  working_set_trimmer_.OnAllFramesInProcessFrozen(process_node_.get());
  // Make sure the working set has shrunk by at least the 10mb allocation.
  EXPECT_GE(working_set_before - 10U,
            GetWorkingSetSizeMb(child_process_.Handle()));
}

TEST_F(WorkingSetTrimmerTest, EmptyWorkingSetInconsistentLaunchTime) {
  // Set the launch time on the process CU to a dummy time.
  process_node_->SetLaunchTime(base::Time::Now() +
                               base::TimeDelta::FromDays(1));

  // When all frames in the Process CU are frozen, the working set of
  // |child_process_| should not be emptied because its creation time is after
  // |process_node_->launch_time()|.
  working_set_trimmer_.OnAllFramesInProcessFrozen(process_node_.get());
  EXPECT_GE(GetWorkingSetSizeMb(child_process_.Handle()), 10U);
}

}  // namespace performance_manager
