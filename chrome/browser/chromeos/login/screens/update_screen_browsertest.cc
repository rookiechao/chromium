// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/screens/update_screen.h"

#include <memory>

#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/login/login_wizard.h"
#include "chrome/browser/chromeos/login/mixin_based_in_process_browser_test.h"
#include "chrome/browser/chromeos/login/screens/base_screen_delegate.h"
#include "chrome/browser/chromeos/login/screens/error_screen.h"
#include "chrome/browser/chromeos/login/screens/network_screen_view.h"
#include "chrome/browser/chromeos/login/screens/update_screen.h"
#include "chrome/browser/chromeos/login/test/js_checker.h"
#include "chrome/browser/chromeos/login/test/network_portal_detector_mixin.h"
#include "chrome/browser/chromeos/login/test/oobe_screen_waiter.h"
#include "chrome/browser/chromeos/login/ui/login_display_host.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_update_engine_client.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_handler.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {

namespace {

const char kStubWifiGuid[] = "wlan0";

std::string GetDownloadingString(int status_resource_id) {
  return l10n_util::GetStringFUTF8(
      IDS_DOWNLOADING, l10n_util::GetStringUTF16(status_resource_id));
}

chromeos::OobeUI* GetOobeUI() {
  auto* host = chromeos::LoginDisplayHost::default_host();
  return host ? host->GetOobeUI() : nullptr;
}

class TestErrorScreenDelegate : public BaseScreenDelegate {
 public:
  explicit TestErrorScreenDelegate(ErrorScreen* error_screen)
      : error_screen_(error_screen) {}

  void set_parent_screen(UpdateScreen* parent_screen) {
    parent_screen_ = parent_screen;
  }

  bool error_screen_shown() const { return error_screen_shown_; }

  // BaseScreenDelegate:
  void ShowCurrentScreen() override {
    error_screen_->Hide();
    parent_screen_->Show();
    error_screen_shown_ = false;
  }
  ErrorScreen* GetErrorScreen() override { return error_screen_; }
  void ShowErrorScreen() override {
    parent_screen_->Hide();
    error_screen_->Show();
    error_screen_shown_ = true;
  }
  void HideErrorScreen(BaseScreen* parent_screen) override {
    EXPECT_EQ(parent_screen, parent_screen_);
    error_screen_->Hide();
    parent_screen_->Show();
    error_screen_shown_ = false;
  }

 private:
  // Owned by OobeUI.
  ErrorScreen* const error_screen_;

  // The update screen under test, that uses this delegate.
  // Should be set using |set_parent_screen()| after the update screen is
  // created.
  BaseScreen* parent_screen_;

  // Whether the error screen is shown by the delegate.
  bool error_screen_shown_ = false;
};

}  // namespace

class UpdateScreenTest : public MixinBasedInProcessBrowserTest {
 public:
  UpdateScreenTest() = default;
  ~UpdateScreenTest() override = default;

  // InProcessBrowserTest:
  void SetUpInProcessBrowserTestFixture() override {
    fake_update_engine_client_ = new FakeUpdateEngineClient();
    chromeos::DBusThreadManager::GetSetterForTesting()->SetUpdateEngineClient(
        std::unique_ptr<UpdateEngineClient>(fake_update_engine_client_));

    MixinBasedInProcessBrowserTest::SetUpInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    ShowLoginWizard(OobeScreen::SCREEN_TEST_NO_WINDOW);

    tick_clock_.Advance(base::TimeDelta::FromMinutes(1));

    error_delegate_ = std::make_unique<TestErrorScreenDelegate>(
        GetOobeUI()->GetErrorScreen());
    update_screen_ = std::make_unique<UpdateScreen>(
        error_delegate_.get(), GetOobeUI()->GetUpdateView(),
        base::BindRepeating(&UpdateScreenTest::HandleScreenExit,
                            base::Unretained(this)));
    update_screen_->set_tick_clock_for_testing(&tick_clock_);
    error_delegate_->set_parent_screen(update_screen_.get());

    MixinBasedInProcessBrowserTest::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    MixinBasedInProcessBrowserTest::TearDownOnMainThread();

    update_screen_.reset();
    error_delegate_.reset();

    base::RunLoop run_loop;
    LoginDisplayHost::default_host()->Finalize(run_loop.QuitClosure());
    run_loop.Run();
  }

 protected:
  void WaitForScreenResult() {
    if (last_screen_result_.has_value())
      return;

    base::RunLoop run_loop;
    screen_result_callback_ = run_loop.QuitClosure();
    run_loop.Run();
  }

  NetworkPortalDetectorMixin network_portal_detector_{&mixin_host_};

  std::unique_ptr<TestErrorScreenDelegate> error_delegate_;
  std::unique_ptr<UpdateScreen> update_screen_;

  FakeUpdateEngineClient* fake_update_engine_client_ = nullptr;  // Unowned.

  base::SimpleTestTickClock tick_clock_;

  base::Optional<UpdateScreen::Result> last_screen_result_;

 private:
  void HandleScreenExit(UpdateScreen::Result result) {
    EXPECT_FALSE(last_screen_result_.has_value());
    last_screen_result_ = result;

    if (screen_result_callback_)
      std::move(screen_result_callback_).Run();
  }

  base::OnceClosure screen_result_callback_;

  DISALLOW_COPY_AND_ASSIGN(UpdateScreenTest);
};

// The wizard controller will not call Show() if the update screen detects that
// there is no update in time - this tests that StartNetworkCheck() on it's own
// does not cause update screen to be shown if no update is found.
IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestUpdateCheckDoneBeforeShow) {
  update_screen_->StartNetworkCheck();

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_IDLE;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  status.status = UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  status.status = UpdateEngineClient::UPDATE_STATUS_IDLE;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  ASSERT_TRUE(last_screen_result_.has_value());
  EXPECT_EQ(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
            last_screen_result_.value());

  ASSERT_NE(GetOobeUI()->current_screen(), OobeScreen::SCREEN_OOBE_UPDATE);

  // Show another screen, and verify the Update screen in not shown before it.
  GetOobeUI()->GetNetworkScreenView()->Show();
  OobeScreenWaiter network_screen_waiter(OobeScreen::SCREEN_OOBE_NETWORK);
  network_screen_waiter.set_assert_next_screen();
  network_screen_waiter.Wait();
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestUpdateNotFoundAfterScreenShow) {
  update_screen_->StartNetworkCheck();

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_IDLE;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  status.status = UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  // If show is called explicitly, the update screen is expected to be shown.
  update_screen_->Show();

  OobeScreenWaiter update_screen_waiter(OobeScreen::SCREEN_OOBE_UPDATE);
  update_screen_waiter.set_assert_next_screen();
  update_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("oobe-update-md");
  test::OobeJS().ExpectVisiblePath(
      {"oobe-update-md", "checking-for-updates-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "cellular-permission-dialog"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "updating-dialog"});

  status.status = UpdateEngineClient::UPDATE_STATUS_IDLE;
  // GetLastStatus() will be called via ExitUpdate() called from
  // UpdateStatusChanged().
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  ASSERT_TRUE(last_screen_result_.has_value());
  EXPECT_EQ(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
            last_screen_result_.value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestUpdateAvailable) {
  update_screen_->set_ignore_update_deadlines_for_testing(true);
  update_screen_->StartNetworkCheck();

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE;
  status.new_version = "latest and greatest";
  status.new_size = 1000000000;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  update_screen_->Show();

  OobeScreenWaiter update_screen_waiter(OobeScreen::SCREEN_OOBE_UPDATE);
  update_screen_waiter.set_assert_next_screen();
  update_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("oobe-update-md");
  test::OobeJS().ExpectVisiblePath(
      {"oobe-update-md", "checking-for-updates-dialog"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "updating-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "cellular-permission-dialog"});

  status.status = UpdateEngineClient::UPDATE_STATUS_UPDATE_AVAILABLE;
  status.download_progress = 0.0;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  status.status = UpdateEngineClient::UPDATE_STATUS_DOWNLOADING;
  status.download_progress = 0.0;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS()
      .CreateWaiter("!$('oobe-update-md').$$('#updating-dialog').hidden")
      ->Wait();
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "checking-for-updates-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "cellular-permission-dialog"});

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          14);
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#progress-message').textContent.trim()",
      l10n_util::GetStringUTF8(IDS_INSTALLING_UPDATE));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(60));
  status.download_progress = 0.01;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          14);
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#estimated-time-left').textContent.trim()",
      GetDownloadingString(IDS_DOWNLOADING_TIME_LEFT_LONG));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(60));
  status.download_progress = 0.08;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          18);
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#estimated-time-left').textContent.trim()",
      GetDownloadingString(IDS_DOWNLOADING_TIME_LEFT_STATUS_ONE_HOUR));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(10));
  status.download_progress = 0.7;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          56);
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#estimated-time-left').textContent.trim()",
      GetDownloadingString(IDS_DOWNLOADING_TIME_LEFT_SMALL));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(10));
  status.download_progress = 0.9;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          68);
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#estimated-time-left').textContent.trim()",
      GetDownloadingString(IDS_DOWNLOADING_TIME_LEFT_SMALL));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(10));
  status.status = UpdateEngineClient::UPDATE_STATUS_VERIFYING;
  status.download_progress = 1.0;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          74);
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#progress-message').textContent.trim()",
      l10n_util::GetStringUTF8(IDS_UPDATE_VERIFYING));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(10));
  status.status = UpdateEngineClient::UPDATE_STATUS_FINALIZING;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          81);
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#progress-message').textContent.trim()",
      l10n_util::GetStringUTF8(IDS_UPDATE_FINALIZING));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  tick_clock_.Advance(base::TimeDelta::FromSeconds(10));
  status.status = UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          100);
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectEQ(
      "$('oobe-update-md').$$('#progress-message').textContent.trim()",
      l10n_util::GetStringUTF8(IDS_UPDATE_FINALIZING));
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "update-complete-msg"});

  // UpdateStatusChanged(status) calls RebootAfterUpdate().
  EXPECT_EQ(1, fake_update_engine_client_->reboot_after_update_call_count());

  // Simulate the situation where reboot does not happen in time.
  ASSERT_TRUE(update_screen_->GetRebootTimerForTesting()->IsRunning());
  update_screen_->GetRebootTimerForTesting()->FireNow();

  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "updating-progress"});
  test::OobeJS().ExpectEQ("$('oobe-update-md').$$('#updating-progress').value",
                          100);
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "estimated-time-left"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "progress-message"});
  test::OobeJS().ExpectVisiblePath({"oobe-update-md", "update-complete-msg"});
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestErrorIssuingUpdateCheck) {
  fake_update_engine_client_->set_update_check_result(
      chromeos::UpdateEngineClient::UPDATE_RESULT_FAILED);
  update_screen_->StartNetworkCheck();

  ASSERT_TRUE(last_screen_result_.has_value());
  EXPECT_EQ(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
            last_screen_result_.value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestErrorCheckingForUpdate) {
  update_screen_->StartNetworkCheck();

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_ERROR;
  // GetLastStatus() will be called via ExitUpdate() called from
  // UpdateStatusChanged().
  fake_update_engine_client_->set_default_status(status);
  update_screen_->UpdateStatusChanged(status);

  ASSERT_TRUE(last_screen_result_.has_value());
  EXPECT_EQ(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
            last_screen_result_.value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestErrorUpdating) {
  update_screen_->StartNetworkCheck();

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_ERROR;
  status.new_version = "latest and greatest";

  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  ASSERT_TRUE(last_screen_result_.has_value());
  EXPECT_EQ(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
            last_screen_result_.value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestTemproraryPortalNetwork) {
  // Change ethernet state to offline.
  network_portal_detector_.SimulateDefaultNetworkState(
      NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PORTAL);

  update_screen_->StartNetworkCheck();

  // If the network is a captive portal network, error message is shown with a
  // delay.
  EXPECT_FALSE(error_delegate_->error_screen_shown());
  EXPECT_TRUE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());

  // If network goes back online, the error message timer should be canceled.
  network_portal_detector_.SimulateDefaultNetworkState(
      NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_ONLINE);

  EXPECT_FALSE(error_delegate_->error_screen_shown());
  EXPECT_FALSE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE;

  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  // Verify that update screen is showing checking for update UI.
  OobeScreenWaiter update_screen_waiter(OobeScreen::SCREEN_OOBE_UPDATE);
  update_screen_waiter.set_assert_next_screen();
  update_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("oobe-update-md");
  test::OobeJS().ExpectVisiblePath(
      {"oobe-update-md", "checking-for-updates-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "cellular-permission-dialog"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "updating-dialog"});

  status.status = UpdateEngineClient::UPDATE_STATUS_IDLE;
  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  ASSERT_TRUE(last_screen_result_.has_value());
  EXPECT_EQ(UpdateScreen::Result::UPDATE_NOT_REQUIRED,
            last_screen_result_.value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestTwoOfflineNetworks) {
  // Change ethernet state to portal.
  network_portal_detector_.SimulateDefaultNetworkState(
      NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PORTAL);
  update_screen_->StartNetworkCheck();

  // Update screen will delay error message about portal state because
  // ethernet is behind captive portal. Simulate the delay timing out.
  EXPECT_FALSE(error_delegate_->error_screen_shown());
  EXPECT_TRUE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());
  update_screen_->GetErrorMessageTimerForTesting()->FireNow();

  EXPECT_FALSE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());
  EXPECT_TRUE(error_delegate_->error_screen_shown());

  OobeScreenWaiter error_screen_waiter(OobeScreen::SCREEN_ERROR_MESSAGE);
  error_screen_waiter.set_assert_next_screen();
  error_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("error-message");
  test::OobeJS().ExpectVisible("error-message-md");
  test::OobeJS().ExpectTrue(
      "$('error-message').classList.contains('ui-state-update')");
  test::OobeJS().ExpectTrue(
      "$('error-message').classList.contains('error-state-portal')");

  // Change active network to the wifi behind proxy.
  network_portal_detector_.SetDefaultNetwork(
      kStubWifiGuid,
      NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PROXY_AUTH_REQUIRED);

  test::OobeJS()
      .CreateWaiter(
          "$('error-message').classList.contains('error-state-proxy')")
      ->Wait();

  EXPECT_FALSE(last_screen_result_.has_value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestVoidNetwork) {
  network_portal_detector_.SimulateNoNetwork();

  // First portal detection attempt returns NULL network and undefined
  // results, so detection is restarted.
  update_screen_->StartNetworkCheck();

  EXPECT_FALSE(error_delegate_->error_screen_shown());
  EXPECT_FALSE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());

  network_portal_detector_.WaitForPortalDetectionRequest();
  network_portal_detector_.SimulateNoNetwork();

  ASSERT_TRUE(error_delegate_->error_screen_shown());
  EXPECT_FALSE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());

  // Second portal detection also returns NULL network and undefined
  // results.  In this case, offline message should be displayed.
  OobeScreenWaiter error_screen_waiter(OobeScreen::SCREEN_ERROR_MESSAGE);
  error_screen_waiter.set_assert_next_screen();
  error_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("error-message");
  test::OobeJS().ExpectVisible("error-message-md");
  test::OobeJS().ExpectTrue(
      "$('error-message').classList.contains('ui-state-update')");
  test::OobeJS().ExpectTrue(
      "$('error-message').classList.contains('error-state-offline')");

  EXPECT_FALSE(last_screen_result_.has_value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, TestAPReselection) {
  network_portal_detector_.SimulateDefaultNetworkState(
      NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PORTAL);

  update_screen_->StartNetworkCheck();

  // Force timer expiration.
  EXPECT_TRUE(update_screen_->GetErrorMessageTimerForTesting()->IsRunning());
  update_screen_->GetErrorMessageTimerForTesting()->FireNow();

  OobeScreenWaiter error_screen_waiter(OobeScreen::SCREEN_ERROR_MESSAGE);
  error_screen_waiter.set_assert_next_screen();
  error_screen_waiter.Wait();

  NetworkHandler::Get()->network_connection_handler()->ConnectToNetwork(
      "fake_path", base::DoNothing(), base::DoNothing(),
      false /* check_error_state */, ConnectCallbackMode::ON_COMPLETED);

  OobeScreenWaiter update_screen_waiter(OobeScreen::SCREEN_OOBE_UPDATE);
  update_screen_waiter.set_assert_next_screen();
  update_screen_waiter.Wait();

  ASSERT_FALSE(last_screen_result_.has_value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, UpdateOverCellularAccepted) {
  update_screen_->set_ignore_update_deadlines_for_testing(true);

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_NEED_PERMISSION_TO_UPDATE;
  status.new_version = "latest and greatest";

  update_screen_->StartNetworkCheck();

  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  OobeScreenWaiter update_screen_waiter(OobeScreen::SCREEN_OOBE_UPDATE);
  update_screen_waiter.set_assert_next_screen();
  update_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("oobe-update-md");
  test::OobeJS().ExpectVisiblePath(
      {"oobe-update-md", "cellular-permission-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "checking-for-updates-dialog"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "updating-dialog"});

  test::OobeJS().TapOnPath({"oobe-update-md", "cellular-permission-next"});

  test::OobeJS()
      .CreateWaiter("!$('oobe-update-md').$$('#updating-dialog').hidden")
      ->Wait();

  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "cellular-permission-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "checking-for-updates-dialog"});

  status.status = UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT;
  update_screen_->UpdateStatusChanged(status);

  // UpdateStatusChanged(status) calls RebootAfterUpdate().
  EXPECT_EQ(1, fake_update_engine_client_->reboot_after_update_call_count());
  ASSERT_FALSE(last_screen_result_.has_value());
}

IN_PROC_BROWSER_TEST_F(UpdateScreenTest, UpdateOverCellularRejected) {
  update_screen_->set_ignore_update_deadlines_for_testing(true);

  UpdateEngineClient::Status status;
  status.status = UpdateEngineClient::UPDATE_STATUS_NEED_PERMISSION_TO_UPDATE;
  status.new_version = "latest and greatest";

  update_screen_->StartNetworkCheck();

  fake_update_engine_client_->set_default_status(status);
  fake_update_engine_client_->NotifyObserversThatStatusChanged(status);

  OobeScreenWaiter update_screen_waiter(OobeScreen::SCREEN_OOBE_UPDATE);
  update_screen_waiter.set_assert_next_screen();
  update_screen_waiter.Wait();

  test::OobeJS().ExpectVisible("oobe-update-md");
  test::OobeJS().ExpectVisiblePath(
      {"oobe-update-md", "cellular-permission-dialog"});
  test::OobeJS().ExpectHiddenPath(
      {"oobe-update-md", "checking-for-updates-dialog"});
  test::OobeJS().ExpectHiddenPath({"oobe-update-md", "updating-dialog"});

  test::OobeJS().TapOnPath({"oobe-update-md", "cellular-permission-back"});

  WaitForScreenResult();
  EXPECT_EQ(UpdateScreen::Result::UPDATE_ERROR, last_screen_result_.value());
}

}  // namespace chromeos
