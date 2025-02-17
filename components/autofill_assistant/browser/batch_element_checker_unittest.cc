// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/batch_element_checker.h"

#include <map>
#include <set>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_task_environment.h"
#include "components/autofill_assistant/browser/mock_run_once_callback.h"
#include "components/autofill_assistant/browser/mock_web_controller.h"
#include "testing/gmock/include/gmock/gmock.h"

using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;

namespace autofill_assistant {

namespace {

class BatchElementCheckerTest : public testing::Test {
 protected:
  BatchElementCheckerTest() : checks_() {}

  void OnElementExistenceCheck(const std::string& name, bool result) {
    element_exists_results_[name] = result;
  }

  BatchElementChecker::ElementCheckCallback ElementExistenceCallback(
      const std::string& name) {
    return base::BindOnce(&BatchElementCheckerTest::OnElementExistenceCheck,
                          base::Unretained(this), name);
  }

  void OnElementVisibilityCheck(const std::string& name, bool result) {
    element_visible_results_[name] = result;
  }

  BatchElementChecker::ElementCheckCallback ElementVisibilityCallback(
      const std::string& name) {
    return base::BindOnce(&BatchElementCheckerTest::OnElementVisibilityCheck,
                          base::Unretained(this), name);
  }

  void OnFieldValueCheck(const std::string& name,
                         bool exists,
                         const std::string& value) {
    get_field_value_results_[name] = value;
  }

  BatchElementChecker::GetFieldValueCallback FieldValueCallback(
      const std::string& name) {
    return base::BindOnce(&BatchElementCheckerTest::OnFieldValueCheck,
                          base::Unretained(this), name);
  }

  void OnDone(const std::string& name) { all_done_.insert(name); }

  base::OnceCallback<void()> DoneCallback(const std::string& name) {
    return base::BindOnce(&BatchElementCheckerTest::OnDone,
                          base::Unretained(this), name);
  }

  void Run(const std::string& callback_name) {
    checks_.Run(&mock_web_controller_, DoneCallback(callback_name));
  }

  MockWebController mock_web_controller_;
  BatchElementChecker checks_;
  std::map<std::string, bool> element_exists_results_;
  std::map<std::string, bool> element_visible_results_;
  std::map<std::string, std::string> get_field_value_results_;
  std::set<std::string> all_done_;
};

TEST_F(BatchElementCheckerTest, Empty) {
  EXPECT_TRUE(checks_.empty());
  checks_.AddElementCheck(kExistenceCheck, Selector({"exists"}),
                          ElementExistenceCallback("exists"));
  EXPECT_FALSE(checks_.empty());
}

TEST_F(BatchElementCheckerTest, OneElementFound) {
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kExistenceCheck, Eq(Selector({"exists"})), _))
      .WillOnce(RunOnceCallback<2>(true));
  checks_.AddElementCheck(kExistenceCheck, Selector({"exists"}),
                          ElementExistenceCallback("exists"));
  Run("was_run");

  EXPECT_THAT(element_exists_results_, Contains(Pair("exists", true)));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, OneElementNotFound) {
  EXPECT_CALL(
      mock_web_controller_,
      OnElementCheck(kExistenceCheck, Eq(Selector({"does_not_exist"})), _))
      .WillOnce(RunOnceCallback<2>(false));
  checks_.AddElementCheck(kExistenceCheck, Selector({"does_not_exist"}),
                          ElementExistenceCallback("does_not_exist"));
  Run("was_run");

  EXPECT_THAT(element_exists_results_, Contains(Pair("does_not_exist", false)));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, OneFieldValueFound) {
  EXPECT_CALL(mock_web_controller_, OnGetFieldValue(Eq(Selector({"field"})), _))
      .WillOnce(RunOnceCallback<1>(true, "some value"));
  checks_.AddFieldValueCheck(Selector({"field"}), FieldValueCallback("field"));
  Run("was_run");

  EXPECT_THAT(get_field_value_results_, Contains(Pair("field", "some value")));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, OneFieldValueNotFound) {
  EXPECT_CALL(mock_web_controller_, OnGetFieldValue(Eq(Selector({"field"})), _))
      .WillOnce(RunOnceCallback<1>(false, ""));
  checks_.AddFieldValueCheck(Selector({"field"}), FieldValueCallback("field"));
  Run("was_run");

  EXPECT_THAT(get_field_value_results_, Contains(Pair("field", "")));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, OneFieldValueEmpty) {
  EXPECT_CALL(mock_web_controller_, OnGetFieldValue(Eq(Selector({"field"})), _))
      .WillOnce(RunOnceCallback<1>(true, ""));
  checks_.AddFieldValueCheck(Selector({"field"}), FieldValueCallback("field"));
  Run("was_run");

  EXPECT_THAT(get_field_value_results_, Contains(Pair("field", "")));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, MultipleElements) {
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kExistenceCheck, Eq(Selector({"1"})), _))
      .WillOnce(RunOnceCallback<2>(true));
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kExistenceCheck, Eq(Selector({"2"})), _))
      .WillOnce(RunOnceCallback<2>(true));
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kExistenceCheck, Eq(Selector({"3"})), _))
      .WillOnce(RunOnceCallback<2>(false));
  EXPECT_CALL(mock_web_controller_, OnGetFieldValue(Eq(Selector({"4"})), _))
      .WillOnce(RunOnceCallback<1>(true, "value"));
  EXPECT_CALL(mock_web_controller_, OnGetFieldValue(Eq(Selector({"5"})), _))
      .WillOnce(RunOnceCallback<1>(false, ""));

  checks_.AddElementCheck(kExistenceCheck, Selector({"1"}),
                          ElementExistenceCallback("1"));
  checks_.AddElementCheck(kExistenceCheck, Selector({"2"}),
                          ElementExistenceCallback("2"));
  checks_.AddElementCheck(kExistenceCheck, Selector({"3"}),
                          ElementExistenceCallback("3"));
  checks_.AddFieldValueCheck(Selector({"4"}), FieldValueCallback("4"));
  checks_.AddFieldValueCheck(Selector({"5"}), FieldValueCallback("5"));
  Run("was_run");

  EXPECT_THAT(element_exists_results_, Contains(Pair("1", true)));
  EXPECT_THAT(element_exists_results_, Contains(Pair("2", true)));
  EXPECT_THAT(element_exists_results_, Contains(Pair("3", false)));
  EXPECT_THAT(get_field_value_results_, Contains(Pair("4", "value")));
  EXPECT_THAT(get_field_value_results_, Contains(Pair("5", "")));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, DeduplicateElementExists) {
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kExistenceCheck, Eq(Selector({"1"})), _))
      .WillOnce(RunOnceCallback<2>(true));
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kExistenceCheck, Eq(Selector({"2"})), _))
      .WillOnce(RunOnceCallback<2>(true));

  checks_.AddElementCheck(kExistenceCheck, Selector({"1"}),
                          ElementExistenceCallback("first 1"));
  checks_.AddElementCheck(kExistenceCheck, Selector({"1"}),
                          ElementExistenceCallback("second 1"));
  checks_.AddElementCheck(kExistenceCheck, Selector({"2"}),
                          ElementExistenceCallback("2"));

  Run("was_run");

  EXPECT_THAT(element_exists_results_, Contains(Pair("first 1", true)));
  EXPECT_THAT(element_exists_results_, Contains(Pair("second 1", true)));
  EXPECT_THAT(element_exists_results_, Contains(Pair("2", true)));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

TEST_F(BatchElementCheckerTest, DeduplicateElementVisible) {
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kVisibilityCheck, Eq(Selector({"1"})), _))
      .WillOnce(RunOnceCallback<2>(true));
  EXPECT_CALL(mock_web_controller_,
              OnElementCheck(kVisibilityCheck, Eq(Selector({"2"})), _))
      .WillOnce(RunOnceCallback<2>(true));

  checks_.AddElementCheck(kVisibilityCheck, Selector({"1"}),
                          ElementVisibilityCallback("first 1"));
  checks_.AddElementCheck(kVisibilityCheck, Selector({"1"}),
                          ElementVisibilityCallback("second 1"));
  checks_.AddElementCheck(kVisibilityCheck, Selector({"2"}),
                          ElementVisibilityCallback("2"));

  Run("was_run");

  EXPECT_THAT(element_visible_results_, Contains(Pair("first 1", true)));
  EXPECT_THAT(element_visible_results_, Contains(Pair("second 1", true)));
  EXPECT_THAT(element_visible_results_, Contains(Pair("2", true)));
  EXPECT_THAT(all_done_, Contains("was_run"));
}

// Deduplicate get field

}  // namespace
}  // namespace autofill_assistant
