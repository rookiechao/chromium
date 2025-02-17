// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_SESSION_MANAGER_FAKE_SESSION_MANAGER_CLIENT_H_
#define CHROMEOS_DBUS_SESSION_MANAGER_FAKE_SESSION_MANAGER_CLIENT_H_

#include <map>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/component_export.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "chromeos/dbus/login_manager/arc.pb.h"
#include "chromeos/dbus/session_manager/session_manager_client.h"

namespace chromeos {

// A fake implementation of session_manager. Accepts policy blobs to be set and
// returns them unmodified.
class COMPONENT_EXPORT(SESSION_MANAGER) FakeSessionManagerClient
    : public SessionManagerClient {
 public:
  enum class PolicyStorageType {
    kOnDisk,    // Store policy in regular files on disk. Usually used for
                // fake D-Bus client implementation, see
                // SessionManagerClient::Create().
    kInMemory,  // Store policy in memory only. Usually used for tests.
  };

  // Constructs a FakeSessionManagerClient with PolicyStorageType == kInMemory.
  // NOTE: This is different from SessionManagerClient::InitializeFake which
  // constructs an instance with PolicyStorageType == kOnDisk. Use
  // SessionManagerClient::InitializeFakeInMemory when replacing this.
  FakeSessionManagerClient();

  explicit FakeSessionManagerClient(PolicyStorageType policy_storage);

  ~FakeSessionManagerClient() override;

  // Returns the fake global instance if initialized. May return null.
  static FakeSessionManagerClient* Get();

  // SessionManagerClient overrides
  void SetStubDelegate(StubDelegate* delegate) override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  bool HasObserver(const Observer* observer) const override;
  void WaitForServiceToBeAvailable(
      WaitForServiceToBeAvailableCallback callback) override;
  bool IsScreenLocked() const override;
  void EmitLoginPromptVisible() override;
  void EmitAshInitialized() override;
  void RestartJob(int socket_fd,
                  const std::vector<std::string>& argv,
                  VoidDBusMethodCallback callback) override;
  void SaveLoginPassword(const std::string& password) override;
  void StartSession(
      const cryptohome::AccountIdentifier& cryptohome_id) override;
  void StopSession() override;
  void StartDeviceWipe() override;
  void ClearForcedReEnrollmentVpd(VoidDBusMethodCallback callback) override;
  void StartTPMFirmwareUpdate(const std::string& update_mode) override;
  void RequestLockScreen() override;
  void NotifyLockScreenShown() override;
  void NotifyLockScreenDismissed() override;
  void RetrieveActiveSessions(ActiveSessionsCallback callback) override;
  void RetrieveDevicePolicy(RetrievePolicyCallback callback) override;
  RetrievePolicyResponseType BlockingRetrieveDevicePolicy(
      std::string* policy_out) override;
  void RetrievePolicyForUser(const cryptohome::AccountIdentifier& cryptohome_id,
                             RetrievePolicyCallback callback) override;
  RetrievePolicyResponseType BlockingRetrievePolicyForUser(
      const cryptohome::AccountIdentifier& cryptohome_id,
      std::string* policy_out) override;
  void RetrievePolicyForUserWithoutSession(
      const cryptohome::AccountIdentifier& cryptohome_id,
      RetrievePolicyCallback callback) override;
  void RetrieveDeviceLocalAccountPolicy(
      const std::string& account_id,
      RetrievePolicyCallback callback) override;
  RetrievePolicyResponseType BlockingRetrieveDeviceLocalAccountPolicy(
      const std::string& account_id,
      std::string* policy_out) override;
  void RetrievePolicy(const login_manager::PolicyDescriptor& descriptor,
                      RetrievePolicyCallback callback) override;
  RetrievePolicyResponseType BlockingRetrievePolicy(
      const login_manager::PolicyDescriptor& descriptor,
      std::string* policy_out) override;
  void StoreDevicePolicy(const std::string& policy_blob,
                         VoidDBusMethodCallback callback) override;
  void StorePolicyForUser(const cryptohome::AccountIdentifier& cryptohome_id,
                          const std::string& policy_blob,
                          VoidDBusMethodCallback callback) override;
  void StoreDeviceLocalAccountPolicy(const std::string& account_id,
                                     const std::string& policy_blob,
                                     VoidDBusMethodCallback callback) override;
  void StorePolicy(const login_manager::PolicyDescriptor& descriptor,
                   const std::string& policy_blob,
                   VoidDBusMethodCallback callback) override;
  bool SupportsRestartToApplyUserFlags() const override;
  void SetFlagsForUser(const cryptohome::AccountIdentifier& cryptohome_id,
                       const std::vector<std::string>& flags) override;
  void GetServerBackedStateKeys(StateKeysCallback callback) override;

  void StartArcMiniContainer(
      const login_manager::StartArcMiniContainerRequest& request,
      StartArcMiniContainerCallback callback) override;
  void UpgradeArcContainer(
      const login_manager::UpgradeArcContainerRequest& request,
      base::OnceClosure success_callback,
      UpgradeErrorCallback error_callback) override;
  void StopArcInstance(VoidDBusMethodCallback callback) override;
  void SetArcCpuRestriction(
      login_manager::ContainerCpuRestrictionState restriction_state,
      VoidDBusMethodCallback callback) override;
  void EmitArcBooted(const cryptohome::AccountIdentifier& cryptohome_id,
                     VoidDBusMethodCallback callback) override;
  void GetArcStartTime(DBusMethodCallback<base::TimeTicks> callback) override;

  // Notifies observers as if ArcInstanceStopped signal is received.
  void NotifyArcInstanceStopped(login_manager::ArcContainerStopReason,
                                const std::string& conainer_instance_id);

  // Returns true if flags for |cryptohome_id| have been set. If the return
  // value is |true|, |*out_flags_for_user| is filled with the flags passed to
  // |SetFlagsForUser|.
  bool GetFlagsForUser(const cryptohome::AccountIdentifier& cryptohome_id,
                       std::vector<std::string>* out_flags_for_user) const;

  // Sets whether FakeSessionManagerClient should advertise (through
  // |SupportsRestartToApplyUserFlags|) that it supports restarting chrome to
  // apply user-session flags. The default is |false|.
  void set_supports_restart_to_apply_user_flags(
      bool supports_restart_to_apply_user_flags) {
    supports_restart_to_apply_user_flags_ =
        supports_restart_to_apply_user_flags;
  }

  // If |force_failure| is true, forces StorePolicy() to fail.
  void ForceStorePolicyFailure(bool force_failure) {
    force_store_policy_failure_ = force_failure;
  }

  // If |force_load_error| is true, forces RetrievePolicy() to succeed with an
  // empty policy blob. This simulates a policy load error in session manager.
  void ForceRetrievePolicyLoadError(bool force_load_error) {
    force_retrieve_policy_load_error_ = force_load_error;
  }

  // Accessors for device policy. Only available for
  // PolicyStorageType::kInMemory.
  const std::string& device_policy() const;
  void set_device_policy(const std::string& policy_blob);

  // Accessors for user policy. Only available for PolicyStorageType::kInMemory.
  const std::string& user_policy(
      const cryptohome::AccountIdentifier& cryptohome_id) const;
  void set_user_policy(const cryptohome::AccountIdentifier& cryptohome_id,
                       const std::string& policy_blob);
  void set_user_policy_without_session(
      const cryptohome::AccountIdentifier& cryptohome_id,
      const std::string& policy_blob);

  // Accessors for device local account policy. Only available for
  // PolicyStorageType::kInMemory.
  const std::string& device_local_account_policy(
      const std::string& account_id) const;
  void set_device_local_account_policy(const std::string& account_id,
                                       const std::string& policy_blob);

  const login_manager::UpgradeArcContainerRequest& last_upgrade_arc_request()
      const {
    return last_upgrade_arc_request_;
  }
  const login_manager::StartArcMiniContainerRequest
  last_start_arc_mini_container_request() const {
    return last_start_arc_mini_container_request_;
  }

  // Notify observers about a property change completion.
  void OnPropertyChangeComplete(bool success);

  // Configures the list of state keys used to satisfy
  // GetServerBackedStateKeys() requests. Only available for
  // PolicyStorageType::kInMemory.
  void set_server_backed_state_keys(
      const std::vector<std::string>& state_keys) {
    DCHECK(policy_storage_ == PolicyStorageType::kInMemory);
    server_backed_state_keys_ = state_keys;
  }

  int clear_forced_re_enrollment_vpd_call_count() const {
    return clear_forced_re_enrollment_vpd_call_count_;
  }

  int start_device_wipe_call_count() const {
    return start_device_wipe_call_count_;
  }
  int request_lock_screen_call_count() const {
    return request_lock_screen_call_count_;
  }

  // Returns how many times LockScreenShown() was called.
  int notify_lock_screen_shown_call_count() const {
    return notify_lock_screen_shown_call_count_;
  }

  // Returns how many times LockScreenDismissed() was called.
  int notify_lock_screen_dismissed_call_count() const {
    return notify_lock_screen_dismissed_call_count_;
  }

  int start_tpm_firmware_update_call_count() const {
    return start_tpm_firmware_update_call_count_;
  }

  void set_arc_available(bool available) { arc_available_ = available; }
  void set_arc_start_time(base::TimeTicks arc_start_time) {
    arc_start_time_ = arc_start_time;
  }

  void set_low_disk(bool low_disk) { low_disk_ = low_disk; }

  const std::string& container_instance_id() const {
    return container_instance_id_;
  }

 private:
  bool supports_restart_to_apply_user_flags_ = false;

  base::ObserverList<Observer>::Unchecked observers_;
  SessionManagerClient::ActiveSessionsMap user_sessions_;
  std::vector<std::string> server_backed_state_keys_;

  // Policy is stored in |policy_| if |policy_storage_| type is
  // PolicyStorageType::kInMemory. Uses the relative stub file path as key.
  const PolicyStorageType policy_storage_;
  std::map<std::string, std::string> policy_;

  // If set to true, StorePolicy() always fails.
  bool force_store_policy_failure_ = false;

  // It set to true, RetrievePolicy() always succeeds with an empty policy blob.
  // This simulates a policy load error in session manager.
  bool force_retrieve_policy_load_error_ = false;

  int clear_forced_re_enrollment_vpd_call_count_;
  int start_device_wipe_call_count_;
  int request_lock_screen_call_count_;
  int notify_lock_screen_shown_call_count_;
  int notify_lock_screen_dismissed_call_count_;
  int start_tpm_firmware_update_call_count_;
  bool screen_is_locked_;

  bool arc_available_;
  base::TimeTicks arc_start_time_;

  bool low_disk_ = false;
  // Pseudo running container id. If not running, empty.
  std::string container_instance_id_;

  // Contains last request passed to StartArcMiniContainer
  login_manager::StartArcMiniContainerRequest
      last_start_arc_mini_container_request_;

  // Contains last requst passed to StartArcInstance
  login_manager::UpgradeArcContainerRequest last_upgrade_arc_request_;

  StubDelegate* delegate_;

  // The last-set flags for user set through |SetFlagsForUser|.
  std::map<cryptohome::AccountIdentifier, std::vector<std::string>>
      flags_for_user_;

  base::WeakPtrFactory<FakeSessionManagerClient> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(FakeSessionManagerClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_SESSION_MANAGER_FAKE_SESSION_MANAGER_CLIENT_H_
