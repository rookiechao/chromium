// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/power/auto_screen_brightness/adapter.h"

#include <map>
#include <numeric>
#include <vector>

#include "ash/public/cpp/ash_pref_names.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/fake_als_reader.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/fake_brightness_monitor.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/fake_model_config_loader.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/modeller.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/monotone_cubic_spline.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/utils.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/dbus/power/fake_power_manager_client.h"
#include "chromeos/dbus/power_manager/backlight.pb.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/testing_pref_store.h"
#include "components/sync_preferences/pref_service_mock_factory.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace power {
namespace auto_screen_brightness {

namespace {

// Checks |actual_avg_log| is equal to the avg log calculated from
// |expected_data|. |expected_data| contains absolute lux value, not log lux.
void CheckAvgLog(const std::vector<double>& expected_data,
                 double actual_avg_log) {
  const size_t count = expected_data.size();
  CHECK_NE(count, 0u);
  const double expected_avg_log =
      std::accumulate(
          expected_data.begin(), expected_data.end(), 0.0,
          [](double sum, double lux) { return sum + ConvertToLog(lux); }) /
      count;
  EXPECT_DOUBLE_EQ(actual_avg_log, expected_avg_log);
}

// Testing modeller.
class FakeModeller : public Modeller {
 public:
  FakeModeller() = default;
  ~FakeModeller() override = default;

  void InitModellerWithCurves(
      const base::Optional<MonotoneCubicSpline>& global_curve,
      const base::Optional<MonotoneCubicSpline>& personal_curve) {
    DCHECK(!modeller_initialized_);
    modeller_initialized_ = true;
    if (global_curve)
      global_curve_.emplace(*global_curve);

    if (personal_curve)
      personal_curve_.emplace(*personal_curve);
  }

  void ReportModelTrained(const MonotoneCubicSpline& personal_curve) {
    DCHECK(modeller_initialized_);
    personal_curve_.emplace(personal_curve);
    for (auto& observer : observers_)
      observer.OnModelTrained(personal_curve);
  }

  void ReportModelInitialized() {
    DCHECK(modeller_initialized_);
    for (auto& observer : observers_)
      observer.OnModelInitialized(global_curve_, personal_curve_);
  }

  // Modeller overrides:
  void AddObserver(Modeller::Observer* observer) override {
    DCHECK(observer);
    observers_.AddObserver(observer);
    if (modeller_initialized_)
      observer->OnModelInitialized(global_curve_, personal_curve_);
  }

  void RemoveObserver(Modeller::Observer* observer) override {
    DCHECK(observer);
    observers_.RemoveObserver(observer);
  }

 private:
  bool modeller_initialized_ = false;
  base::Optional<MonotoneCubicSpline> global_curve_;
  base::Optional<MonotoneCubicSpline> personal_curve_;

  base::ObserverList<Observer> observers_;
};

class TestObserver : public PowerManagerClient::Observer {
 public:
  TestObserver() = default;
  ~TestObserver() override = default;

  // chromeos::PowerManagerClient::Observer overrides:
  void ScreenBrightnessChanged(
      const power_manager::BacklightBrightnessChange& change) override {
    ++num_changes_;
    change_ = change;
  }
  double GetBrightnessPercent() const { return change_.percent(); }

  int num_changes() const { return num_changes_; }

  power_manager::BacklightBrightnessChange_Cause GetCause() const {
    return change_.cause();
  }

 private:
  int num_changes_ = 0;
  power_manager::BacklightBrightnessChange change_;
};

}  // namespace

class AdapterTest : public testing::Test {
 public:
  AdapterTest()
      : thread_bundle_(
            base::test::ScopedTaskEnvironment::MainThreadType::MOCK_TIME) {}

  ~AdapterTest() override = default;

  void SetUp() override {
    chromeos::PowerManagerClient::InitializeFake();
    power_manager::SetBacklightBrightnessRequest request;
    request.set_percent(1);
    chromeos::PowerManagerClient::Get()->SetScreenBrightness(request);
    thread_bundle_.RunUntilIdle();

    chromeos::PowerManagerClient::Get()->AddObserver(&test_observer_);

    global_curve_.emplace(MonotoneCubicSpline({-4, 12, 20}, {30, 80, 100}));
    personal_curve_.emplace(MonotoneCubicSpline({-4, 12, 20}, {20, 60, 100}));
  }

  void TearDown() override {
    adapter_.reset();
    base::TaskScheduler::GetInstance()->FlushForTesting();
    chromeos::PowerManagerClient::Shutdown();
  }

  // Creates Adapter only, but its input may or may not be ready.
  void SetUpAdapter(const std::map<std::string, std::string>& params,
                    bool brightness_set_by_policy = false) {
    // Simulate the real clock that will not produce TimeTicks equal to 0.
    // This is because the Adapter will treat 0 TimeTicks are uninitialized
    // values.
    thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
    sync_preferences::PrefServiceMockFactory factory;
    factory.set_user_prefs(base::WrapRefCounted(new TestingPrefStore()));
    scoped_refptr<user_prefs::PrefRegistrySyncable> registry(
        new user_prefs::PrefRegistrySyncable);

    chromeos::power::auto_screen_brightness::MetricsReporter::
        RegisterLocalStatePrefs(registry.get());

    // Same default values as used in the actual pref store.
    registry->RegisterIntegerPref(ash::prefs::kPowerAcScreenBrightnessPercent,
                                  -1, PrefRegistry::PUBLIC);
    registry->RegisterIntegerPref(
        ash::prefs::kPowerBatteryScreenBrightnessPercent, -1,
        PrefRegistry::PUBLIC);

    sync_preferences::PrefServiceSyncable* regular_prefs =
        factory.CreateSyncable(registry.get()).release();

    RegisterUserProfilePrefs(registry.get());
    if (brightness_set_by_policy) {
      regular_prefs->SetInteger(ash::prefs::kPowerAcScreenBrightnessPercent,
                                10);
      regular_prefs->SetInteger(
          ash::prefs::kPowerBatteryScreenBrightnessPercent, 10);
    }

    CHECK(temp_dir_.CreateUniqueTempDir());
    TestingProfile::Builder profile_builder;
    profile_builder.SetProfileName("testuser@gmail.com");
    profile_builder.SetPath(temp_dir_.GetPath().AppendASCII("TestProfile"));
    profile_builder.SetPrefService(base::WrapUnique(regular_prefs));

    profile_ = profile_builder.Build();

    if (!params.empty()) {
      scoped_feature_list_.InitAndEnableFeatureWithParameters(
          features::kAutoScreenBrightness, params);
    }

    adapter_ = Adapter::CreateForTesting(
        profile_.get(), &fake_als_reader_, &fake_brightness_monitor_,
        &fake_modeller_, &fake_model_config_loader_,
        nullptr /* metrics_reporter */, chromeos::PowerManagerClient::Get(),
        thread_bundle_.GetMockTickClock());
    thread_bundle_.RunUntilIdle();
  }

  // Sets up all required input for Adapter and then creates Adapter.
  void Init(AlsReader::AlsInitStatus als_reader_status,
            BrightnessMonitor::Status brightness_monitor_status,
            const base::Optional<MonotoneCubicSpline>& global_curve,
            const base::Optional<MonotoneCubicSpline>& personal_curve,
            const base::Optional<ModelConfig>& model_config,
            const std::map<std::string, std::string>& params,
            bool brightness_set_by_policy = false) {
    fake_als_reader_.set_als_init_status(als_reader_status);
    fake_brightness_monitor_.set_status(brightness_monitor_status);
    fake_modeller_.InitModellerWithCurves(global_curve, personal_curve);
    if (model_config) {
      fake_model_config_loader_.set_model_config(model_config.value());
    }

    SetUpAdapter(params, brightness_set_by_policy);
  }

  void ReportSuspendDone() {
    chromeos::FakePowerManagerClient::Get()->SendSuspendDone();
    thread_bundle_.RunUntilIdle();
  }

  // Returns a valid ModelConfig.
  ModelConfig GetTestModelConfig(const std::string& metrics_key = "abc") {
    ModelConfig model_config;
    model_config.auto_brightness_als_horizon_seconds = 5.0;
    model_config.log_lux = {
        3.69, 4.83, 6.54, 7.68, 8.25, 8.82,
    };
    model_config.brightness = {
        36.14, 47.62, 85.83, 93.27, 93.27, 100,
    };

    model_config.metrics_key = metrics_key;
    model_config.model_als_horizon_seconds = 3.0;
    return model_config;
  }

  void ReportAls(int als_value) {
    fake_als_reader_.ReportAmbientLightUpdate(als_value);
    thread_bundle_.RunUntilIdle();
  }

  void ReportUserBrightnessChangeRequest(double old_brightness_percent,
                                         double new_brightness_percent) {
    fake_brightness_monitor_.ReportUserBrightnessChanged(
        old_brightness_percent, new_brightness_percent);
    fake_brightness_monitor_.ReportUserBrightnessChangeRequested();
    thread_bundle_.RunUntilIdle();
  }

  // Forwards time first and then reports Als.
  void ForwardTimeAndReportAls(const std::vector<int>& als_values) {
    for (const int als_value : als_values) {
      // Forward 1 second to simulate the real AlsReader that samples data at
      // 1hz.
      thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
      ReportAls(als_value);
    }
  }

 protected:
  content::TestBrowserThreadBundle thread_bundle_;

  TestObserver test_observer_;

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<TestingProfile> profile_;

  base::Optional<MonotoneCubicSpline> global_curve_;
  base::Optional<MonotoneCubicSpline> personal_curve_;

  FakeAlsReader fake_als_reader_;
  FakeBrightnessMonitor fake_brightness_monitor_;
  FakeModeller fake_modeller_;
  FakeModelConfigLoader fake_model_config_loader_;

  base::HistogramTester histogram_tester_;

  // |brightening_log_lux_threshold| and |darkening_log_lux_threshold| are set
  // to very small values so a slight change in ALS would trigger brightness
  // update. |stabilization_threshold| is set to a very high value so that we
  // don't have to check ALS has stablized.
  const std::map<std::string, std::string> default_params_ = {
      {"brightening_log_lux_threshold", "0.00001"},
      {"darkening_log_lux_threshold", "0.00001"},
      {"stabilization_threshold", "100000000"},
      {"model_curve", "2"},
      {"auto_brightness_als_horizon_seconds", "5"},
      {"user_adjustment_effect", "0"},
  };

  base::test::ScopedFeatureList scoped_feature_list_;

  std::unique_ptr<Adapter> adapter_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AdapterTest);
};

// AlsReader is |kDisabled| when Adapter is created.
TEST_F(AdapterTest, AlsReaderDisabledOnInit) {
  Init(AlsReader::AlsInitStatus::kDisabled, BrightnessMonitor::Status::kSuccess,
       global_curve_, base::nullopt /* personal_curve */, GetTestModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// BrightnessMonitor is |kDisabled| when Adapter is created.
TEST_F(AdapterTest, BrightnessMonitorDisabledOnInit) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kDisabled,
       global_curve_, base::nullopt /* personal_curve */, GetTestModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// Modeller is |kDisabled| when Adapter is created.
TEST_F(AdapterTest, ModellerDisabledOnInit) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       base::nullopt /* global_curve */, base::nullopt /* personal_curve */,
       GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// ModelConfigLoader has an invalid config, hence Modeller is disabled.
TEST_F(AdapterTest, ModelConfigLoaderDisabledOnInit) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, base::nullopt /* personal_curve */, ModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// AlsReader is |kDisabled| on later notification.
TEST_F(AdapterTest, AlsReaderDisabledOnNotification) {
  Init(AlsReader::AlsInitStatus::kInProgress,
       BrightnessMonitor::Status::kSuccess, global_curve_,
       base::nullopt /* personal_curve */, GetTestModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_als_reader_.set_als_init_status(AlsReader::AlsInitStatus::kDisabled);
  fake_als_reader_.ReportReaderInitialized();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// AlsReader is |kSuccess| on later notification.
TEST_F(AdapterTest, AlsReaderEnabledOnNotification) {
  Init(AlsReader::AlsInitStatus::kInProgress,
       BrightnessMonitor::Status::kSuccess, global_curve_,
       base::nullopt /* personal_curve */, GetTestModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_als_reader_.set_als_init_status(AlsReader::AlsInitStatus::kSuccess);
  fake_als_reader_.ReportReaderInitialized();
  thread_bundle_.RunUntilIdle();

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_FALSE(adapter_->GetPersonalCurveForTesting());
}

// BrightnessMonitor is |kDisabled| on later notification.
TEST_F(AdapterTest, BrightnessMonitorDisabledOnNotification) {
  Init(AlsReader::AlsInitStatus::kSuccess,
       BrightnessMonitor::Status::kInitializing, global_curve_,
       base::nullopt /* personal_curve */, GetTestModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_brightness_monitor_.set_status(BrightnessMonitor::Status::kDisabled);
  fake_brightness_monitor_.ReportBrightnessMonitorInitialized();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// BrightnessMonitor is |kSuccess| on later notification.
TEST_F(AdapterTest, BrightnessMonitorEnabledOnNotification) {
  Init(AlsReader::AlsInitStatus::kSuccess,
       BrightnessMonitor::Status::kInitializing, global_curve_,
       base::nullopt /* personal_curve */, GetTestModelConfig(),
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_brightness_monitor_.set_status(BrightnessMonitor::Status::kSuccess);
  fake_brightness_monitor_.ReportBrightnessMonitorInitialized();
  thread_bundle_.RunUntilIdle();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_FALSE(adapter_->GetPersonalCurveForTesting());
}

// Modeller is |kDisabled| on later notification.
TEST_F(AdapterTest, ModellerDisabledOnNotification) {
  fake_als_reader_.set_als_init_status(AlsReader::AlsInitStatus::kSuccess);
  fake_brightness_monitor_.set_status(BrightnessMonitor::Status::kSuccess);
  fake_model_config_loader_.set_model_config(GetTestModelConfig());
  SetUpAdapter(default_params_);
  thread_bundle_.RunUntilIdle();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_modeller_.InitModellerWithCurves(base::nullopt, base::nullopt);
  fake_modeller_.ReportModelInitialized();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
  EXPECT_FALSE(adapter_->GetGlobalCurveForTesting());
  EXPECT_FALSE(adapter_->GetPersonalCurveForTesting());
}

// Modeller is |kSuccess| on later notification.
TEST_F(AdapterTest, ModellerEnabledOnNotification) {
  fake_als_reader_.set_als_init_status(AlsReader::AlsInitStatus::kSuccess);
  fake_brightness_monitor_.set_status(BrightnessMonitor::Status::kSuccess);
  fake_model_config_loader_.set_model_config(GetTestModelConfig());
  SetUpAdapter(default_params_);
  thread_bundle_.RunUntilIdle();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_modeller_.InitModellerWithCurves(global_curve_, personal_curve_);
  fake_modeller_.ReportModelInitialized();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);
}

// ModelConfigLoader reports an invalid config on later notification.
TEST_F(AdapterTest, InvalidModelConfigOnNotification) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, base::nullopt /* personal_curve */, base::nullopt,
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  // ModelConfig() creates an invalid config.
  DCHECK(!IsValidModelConfig(ModelConfig()));
  fake_model_config_loader_.set_model_config(ModelConfig());
  fake_model_config_loader_.ReportModelConfigLoaded();
  thread_bundle_.RunUntilIdle();

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);
}

// ModelConfigLoader reports a valid config on later notification.
TEST_F(AdapterTest, ValidModelConfigOnNotification) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, base::nullopt /* personal_curve */, base::nullopt,
       default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kInitializing);

  fake_model_config_loader_.set_model_config(GetTestModelConfig());
  fake_model_config_loader_.ReportModelConfigLoaded();
  thread_bundle_.RunUntilIdle();

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_FALSE(adapter_->GetPersonalCurveForTesting());
}

// First ALS comes in 1 second after AlsReader is initialized. Hence after
// |auto_brightness_als_horizon_seconds|, brightness is changed.
TEST_F(AdapterTest, FirstAlsAfterAlsReaderInitTime) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  // |auto_brightness_als_horizon_seconds| is 5.
  ForwardTimeAndReportAls({1, 2, 3, 4});
  EXPECT_EQ(test_observer_.num_changes(), 0);

  ForwardTimeAndReportAls({100});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 100},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// First ALS comes in at the same time when AlsReader is initialized. Hence
// after |auto_brightness_als_horizon_seconds| + 1 readings, brightness is
// changed.
TEST_F(AdapterTest, FirstAlsAtAlsReaderInitTime) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  // First ALS when AlsReader is initialized.
  ReportAls(10);
  ForwardTimeAndReportAls({1, 2, 3, 4});
  EXPECT_EQ(test_observer_.num_changes(), 0);

  ForwardTimeAndReportAls({100});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 100},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

TEST_F(AdapterTest, SequenceOfBrightnessUpdatesWithDefaultParams) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);

  ForwardTimeAndReportAls({1, 2, 3, 4});
  EXPECT_EQ(test_observer_.num_changes(), 0);

  // Brightness is changed for the first time after the 5th reading.
  ForwardTimeAndReportAls({5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Several other ALS readings come in, but need to wait for
  // |params.auto_brightness_als_horizon_seconds| to pass before having any
  // effect
  ForwardTimeAndReportAls({20});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({30});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({40});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({50});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // The next ALS reading triggers brightness change.
  ForwardTimeAndReportAls({60});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({20, 30, 40, 50, 60},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // |params.auto_brightness_als_horizon_seconds| has elapsed since we've made
  // the change, but there's no new ALS value, hence no brightness change is
  // triggered.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(10));
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({20, 30, 40, 50, 60},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  EXPECT_EQ(adapter_->GetAverageAmbientWithStdDevForTesting(
                thread_bundle_.NowTicks()),
            base::nullopt);

  // A new ALS value triggers a brightness change.
  ForwardTimeAndReportAls({100});
  EXPECT_EQ(test_observer_.num_changes(), 3);
  CheckAvgLog({100}, adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// A user brightness change comes in when ALS readings exist. This also disables
// the adapter because |user_adjustment_effect| is 0 (disabled).
TEST_F(AdapterTest, UserBrightnessChangeAlsReadingExists) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  ForwardTimeAndReportAls({1, 2, 3, 4});
  EXPECT_EQ(test_observer_.num_changes(), 0);

  // Adapter will not be applied after a user manual adjustment.
  ReportUserBrightnessChangeRequest(20.0, 30.0);

  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", false, 1);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());
  CheckAvgLog({1, 2, 3, 4}, adapter_->GetCurrentAvgLogAlsForTesting().value());

  // An als reading comes in but will not change the brightness.
  ForwardTimeAndReportAls({100});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  CheckAvgLog({1, 2, 3, 4}, adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Another user manual adjustment comes in.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
  ReportUserBrightnessChangeRequest(30.0, 40.0);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());
  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", false, 2);
  CheckAvgLog({2, 3, 4, 100},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// Same as |UserBrightnessChangeAlsReadingExists| except that user adjustment
// effect is Continue.
TEST_F(AdapterTest, UserBrightnessChangeAlsReadingExistsContinue) {
  std::map<std::string, std::string> params = default_params_;
  // UserAdjustmentEffect::kContinueAuto = 2.
  params["user_adjustment_effect"] = "2";
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  ForwardTimeAndReportAls({2, 4, 6, 8});
  EXPECT_EQ(test_observer_.num_changes(), 0);

  // User brightness change comes in.
  ReportUserBrightnessChangeRequest(20.0, 30.0);
  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", false, 1);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());
  EXPECT_EQ(test_observer_.num_changes(), 0);
  CheckAvgLog({2, 4, 6, 8}, adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Four ALS readings come in, but not enough time has passed since user
  // brightness change.
  ForwardTimeAndReportAls({4, 6, 8, 2});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  CheckAvgLog({2, 4, 6, 8}, adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Another ALS reading is in and triggers brightness change.
  ForwardTimeAndReportAls({5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({4, 6, 8, 2, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Another user manual adjustment comes in.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
  ReportUserBrightnessChangeRequest(30.0, 40.0);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());
  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", false, 3);
  CheckAvgLog({6, 8, 2, 5}, adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// Same as |UserBrightnessChangeAlsReadingExists| except that the 1st user
// brightness change comes when there is no ALS reading.
TEST_F(AdapterTest, UserBrightnessChangeAlsReadingAbsent) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  // Adapter will not be applied after a user manual adjustment.
  ReportUserBrightnessChangeRequest(20.0, 30.0);

  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", true, 1);
  EXPECT_EQ(adapter_->GetCurrentAvgLogAlsForTesting(), base::nullopt);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());
  EXPECT_FALSE(adapter_->GetCurrentAvgLogAlsForTesting());

  // ALS readings come in but will not change the brightness.
  ForwardTimeAndReportAls({100, 101, 102, 103, 104});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  EXPECT_FALSE(adapter_->GetCurrentAvgLogAlsForTesting());

  // Another user manual adjustment comes in.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
  ReportUserBrightnessChangeRequest(30.0, 40.0);
  histogram_tester_.ExpectBucketCount(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", true, 1);
  histogram_tester_.ExpectBucketCount(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", false, 1);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());
  CheckAvgLog({101, 102, 103, 104},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// Same as |UserBrightnessChangeAlsReadingAbsent| except that user adjustment
// effect is Continue.
TEST_F(AdapterTest, UserBrightnessChangeAlsReadingAbsentContinue) {
  std::map<std::string, std::string> params = default_params_;
  // UserAdjustmentEffect::kContinueAuto = 2.
  params["user_adjustment_effect"] = "2";
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  ReportUserBrightnessChangeRequest(20.0, 30.0);

  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", true, 1);
  EXPECT_EQ(adapter_->GetCurrentAvgLogAlsForTesting(), base::nullopt);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());
  EXPECT_FALSE(adapter_->GetCurrentAvgLogAlsForTesting());

  // ALS readings come in, and will trigger a brightness change.
  ForwardTimeAndReportAls({100});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  ForwardTimeAndReportAls({101, 102, 103, 104});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({100, 101, 102, 103, 104},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Another user manual adjustment comes in.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
  ReportUserBrightnessChangeRequest(30.0, 40.0);
  histogram_tester_.ExpectBucketCount(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", true, 1);
  histogram_tester_.ExpectBucketCount(
      "AutoScreenBrightness.MissingAlsWhenBrightnessChanged", false, 2);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());
  CheckAvgLog({101, 102, 103, 104},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// Set |brightening_log_lux_threshold| to a very high value to effectively make
// brightening impossible.
TEST_F(AdapterTest, BrighteningThreshold) {
  std::map<std::string, std::string> params = default_params_;
  params["brightening_log_lux_threshold"] = "100";
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);

  ForwardTimeAndReportAls({1, 2, 3, 4});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  ForwardTimeAndReportAls({5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 100);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 0.00001);

  ForwardTimeAndReportAls({4, 4, 4, 4, 4});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 100);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 0.00001);

  // Darkening is still possible.
  ForwardTimeAndReportAls({1});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 100);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 0.00001);

  ForwardTimeAndReportAls({1});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({4, 4, 4, 1, 1},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 100);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 0.00001);
}

// Set |darkening_log_lux_threshold| to a very high value to effectively make
// darkening impossible.
TEST_F(AdapterTest, DarkeningThreshold) {
  std::map<std::string, std::string> params = default_params_;
  params["darkening_log_lux_threshold"] = "100";
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  ForwardTimeAndReportAls({10, 20, 30, 40});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  ForwardTimeAndReportAls({50});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({10, 20, 30, 40, 50},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 0.00001);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 100);

  ForwardTimeAndReportAls({25, 25, 25, 25, 25});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({10, 20, 30, 40, 50},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 0.00001);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 100);

  ForwardTimeAndReportAls({40});
  CheckAvgLog({25, 25, 25, 25, 40},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_DOUBLE_EQ(adapter_->GetBrighteningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() + 0.00001);
  EXPECT_DOUBLE_EQ(adapter_->GetDarkeningThresholdForTesting(),
                   adapter_->GetCurrentAvgLogAlsForTesting().value() - 100);
}

// Set |stabilization_threshold| to a very low value so that the average really
// should have little fluctuations before we change brightness.
TEST_F(AdapterTest, StablizationThreshold) {
  std::map<std::string, std::string> params = default_params_;
  params["stabilization_threshold"] = "0.00001";
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  ForwardTimeAndReportAls({10, 20, 30, 40, 50});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({10, 20, 30, 40, 50},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // A fluctuation means brightness is not changed.
  ForwardTimeAndReportAls({29, 29, 29, 29, 20});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({10, 20, 30, 40, 50},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({20, 20, 20, 20});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({20, 20, 20, 20, 20},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// Shorten |auto_brightness_als_horizon| to 1 second. Averaging period is
// shorter and |stabilization_threshold| is ineffective in regularizing
// stabilization.
TEST_F(AdapterTest, AlsHorizon) {
  std::map<std::string, std::string> params = default_params_;
  params["auto_brightness_als_horizon_seconds"] = "1";
  // Small |stabilization_threshold|.
  params["stabilization_threshold"] = "0.00001";
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  ForwardTimeAndReportAls({10});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({10}, adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({100});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({100}, adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({2});
  EXPECT_EQ(test_observer_.num_changes(), 3);
  CheckAvgLog({2}, adapter_->GetCurrentAvgLogAlsForTesting().value());
}

TEST_F(AdapterTest, UsePersonalCurve) {
  std::map<std::string, std::string> params = default_params_;
  params["model_curve"] = "1";

  // Init modeller with only a global curve.
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, base::nullopt /* personal_curve */, GetTestModelConfig(),
       params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  // Sufficient ALS data has come in but no brightness change is triggered
  // because there is no personal curve.
  ForwardTimeAndReportAls({1, 2, 3, 4, 5, 6, 7, 8});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  EXPECT_EQ(adapter_->GetCurrentAvgLogAlsForTesting(), base::nullopt);

  // Personal curve is received, it does not lead to any immediate brightness
  // change.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(1));
  fake_modeller_.ReportModelTrained(*personal_curve_);
  EXPECT_EQ(test_observer_.num_changes(), 0);
  EXPECT_EQ(adapter_->GetCurrentAvgLogAlsForTesting(), base::nullopt);

  // Another ALS comes in, which triggers a brightness change.
  ReportAls(20);
  EXPECT_EQ(test_observer_.num_changes(), 1);
  EXPECT_EQ(test_observer_.GetCause(),
            power_manager::BacklightBrightnessChange_Cause_MODEL);

  CheckAvgLog({5, 6, 7, 8, 20},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Brightness is changed according to the personal curve.
  EXPECT_DOUBLE_EQ(test_observer_.GetBrightnessPercent(),
                   personal_curve_->Interpolate(
                       adapter_->GetCurrentAvgLogAlsForTesting().value()));
}

TEST_F(AdapterTest, UseGlobalCurve) {
  std::map<std::string, std::string> params = default_params_;
  params["model_curve"] = "0";

  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  ForwardTimeAndReportAls({1, 2, 3, 4, 5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Brightness is changed according to the global curve.
  EXPECT_DOUBLE_EQ(test_observer_.GetBrightnessPercent(),
                   global_curve_->Interpolate(
                       adapter_->GetCurrentAvgLogAlsForTesting().value()));

  // A new personal curve is received but adapter still uses the global curve.
  thread_bundle_.FastForwardBy(base::TimeDelta::FromSeconds(20));
  fake_modeller_.ReportModelTrained(*personal_curve_);
  ReportAls(20);
  EXPECT_EQ(test_observer_.num_changes(), 2);
  EXPECT_EQ(test_observer_.GetCause(),
            power_manager::BacklightBrightnessChange_Cause_MODEL);

  // Brightness is changed according to the global curve.
  EXPECT_DOUBLE_EQ(test_observer_.GetBrightnessPercent(),
                   global_curve_->Interpolate(
                       adapter_->GetCurrentAvgLogAlsForTesting().value()));
}

TEST_F(AdapterTest, BrightnessSetByPolicy) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_,
       true /* brightness_set_by_policy */);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);

  ForwardTimeAndReportAls({1, 2, 3, 4, 5, 6, 7, 8});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  EXPECT_EQ(adapter_->GetCurrentAvgLogAlsForTesting(), base::nullopt);
}

TEST_F(AdapterTest, FeatureDisabled) {
  // An empty param map will not enable the experiment.
  std::map<std::string, std::string> empty_params;

  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), empty_params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kDisabled);

  // Global and personal curves are received, but they won't be used to change
  // brightness.
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());

  // No brightness is changed.
  ForwardTimeAndReportAls({1, 2, 3, 4, 5, 6, 7, 8});
  EXPECT_EQ(test_observer_.num_changes(), 0);
  EXPECT_EQ(adapter_->GetCurrentAvgLogAlsForTesting(), base::nullopt);
}

TEST_F(AdapterTest, ValidParameters) {
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  histogram_tester_.ExpectTotalCount("AutoScreenBrightness.ParameterError", 0);
}

TEST_F(AdapterTest, InvalidParameters) {
  std::map<std::string, std::string> params = default_params_;
  params["user_adjustment_effect"] = "10";

  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  histogram_tester_.ExpectUniqueSample(
      "AutoScreenBrightness.ParameterError",
      static_cast<int>(ParameterError::kAdapterError), 1);
}

TEST_F(AdapterTest, UserAdjustmentEffectDisable) {
  // |default_params_| sets the effect to disable.
  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), default_params_);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);

  // Brightness is changed for the 1st time.
  ForwardTimeAndReportAls({1, 2, 3, 4, 5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Adapter will not be applied after a user manual adjustment.
  ReportUserBrightnessChangeRequest(20.0, 30.0);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());

  ForwardTimeAndReportAls({6, 7, 8, 9, 10, 11});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // SuspendDone is received, which does not enable Adapter.
  ReportSuspendDone();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());

  ForwardTimeAndReportAls({11, 12, 13, 14, 15, 16});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

TEST_F(AdapterTest, UserAdjustmentEffectPause) {
  std::map<std::string, std::string> params = default_params_;
  // UserAdjustmentEffect::kPauseAuto = 1.
  params["user_adjustment_effect"] = "1";

  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);

  // Brightness is changed for the 1st time.
  ForwardTimeAndReportAls({1, 2, 3, 4, 5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // User manually changes brightness so that adapter will not be applied.
  ReportUserBrightnessChangeRequest(20.0, 30.0);
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());

  // New ALS data will not trigger brightness update.
  ForwardTimeAndReportAls({101, 102, 103, 104, 105, 106, 107, 108});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // // SuspendDone is received, which reenables adapter.
  ReportSuspendDone();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());

  // Another ALS results in a brightness change.
  ForwardTimeAndReportAls({109});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({105, 106, 107, 108, 109},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // Another user brightness change.
  ReportUserBrightnessChangeRequest(40.0, 50.0);
  CheckAvgLog({105, 106, 107, 108, 109},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_FALSE(adapter_->IsAppliedForTesting());

  // New ALS data will not trigger brightness update.
  ForwardTimeAndReportAls({200});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({105, 106, 107, 108, 109},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  // SuspendDone is received, which reenables adapter.
  ReportSuspendDone();
  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());

  // Als readings come in but not sufficient time since user changed brightness.
  ForwardTimeAndReportAls({201, 202, 203});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({105, 106, 107, 108, 109},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({204});
  EXPECT_EQ(test_observer_.num_changes(), 3);
  CheckAvgLog({200, 201, 202, 203, 204},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

TEST_F(AdapterTest, UserAdjustmentEffectContinue) {
  std::map<std::string, std::string> params = default_params_;
  // UserAdjustmentEffect::kContinueAuto = 2.
  params["user_adjustment_effect"] = "2";

  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig(), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);

  // Brightness is changed for the 1st time.
  ForwardTimeAndReportAls({1, 2, 3, 4, 5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({10});
  // User manual adjustment doesn't disable adapter.
  ReportUserBrightnessChangeRequest(40.0, 50.0);
  CheckAvgLog({2, 3, 4, 5, 10},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());

  ForwardTimeAndReportAls({100, 101, 102, 103});
  CheckAvgLog({2, 3, 4, 5, 10},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({104});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({100, 101, 102, 103, 104},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

// Default user adjustment effect for atlas is Continue.
TEST_F(AdapterTest, UserAdjustmentEffectContinueDefaultForAtlas) {
  std::map<std::string, std::string> params = default_params_;
  // User adjustment effect for Atlas is only Continue when it's not explicitly
  // set by the finch params.
  params.erase("user_adjustment_effect");

  Init(AlsReader::AlsInitStatus::kSuccess, BrightnessMonitor::Status::kSuccess,
       global_curve_, personal_curve_, GetTestModelConfig("atlas"), params);

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->GetGlobalCurveForTesting());
  EXPECT_EQ(*adapter_->GetGlobalCurveForTesting(), *global_curve_);
  EXPECT_TRUE(adapter_->GetPersonalCurveForTesting());
  EXPECT_EQ(*adapter_->GetPersonalCurveForTesting(), *personal_curve_);

  // Brightness is changed for the 1st time.
  ForwardTimeAndReportAls({1, 2, 3, 4, 5});
  EXPECT_EQ(test_observer_.num_changes(), 1);
  CheckAvgLog({1, 2, 3, 4, 5},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({10});
  // User manual adjustment doesn't disable adapter.
  ReportUserBrightnessChangeRequest(40.0, 50.0);
  CheckAvgLog({2, 3, 4, 5, 10},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  EXPECT_EQ(adapter_->GetStatusForTesting(), Adapter::Status::kSuccess);
  EXPECT_TRUE(adapter_->IsAppliedForTesting());

  ForwardTimeAndReportAls({100, 101, 102, 103});
  CheckAvgLog({2, 3, 4, 5, 10},
              adapter_->GetCurrentAvgLogAlsForTesting().value());

  ForwardTimeAndReportAls({104});
  EXPECT_EQ(test_observer_.num_changes(), 2);
  CheckAvgLog({100, 101, 102, 103, 104},
              adapter_->GetCurrentAvgLogAlsForTesting().value());
}

}  // namespace auto_screen_brightness
}  // namespace power
}  // namespace chromeos
