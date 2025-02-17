// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/autofill/ios/browser/autofill_agent.h"

#include "base/strings/utf_string_conversions.h"
#import "base/test/ios/wait_util.h"
#include "base/test/scoped_feature_list.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/popup_item_ids.h"
#include "components/autofill/core/browser/suggestion.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/ios/browser/autofill_driver_ios.h"
#import "components/autofill/ios/browser/js_autofill_manager.h"
#include "components/prefs/pref_service.h"
#include "ios/web/public/test/fakes/fake_web_frame.h"
#include "ios/web/public/test/fakes/test_browser_state.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#include "ios/web/public/test/test_web_thread_bundle.h"
#import "ios/web/public/web_state/js/crw_js_injection_receiver.h"
#include "ios/web/public/web_state/web_frame_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#include "third_party/ocmock/gtest_support.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using autofill::POPUP_ITEM_ID_CLEAR_FORM;
using autofill::POPUP_ITEM_ID_GOOGLE_PAY_BRANDING;
using base::test::ios::WaitUntilCondition;

// Subclass of web::FakeWebFrame that allow to set a callback before any
// JavaScript call. This callback can be used to check the state of the page.
class FakeWebFrameCallback : public web::FakeWebFrame {
 public:
  FakeWebFrameCallback(const std::string& frame_id,
                       bool is_main_frame,
                       GURL security_origin,
                       std::function<void()> callback)
      : web::FakeWebFrame(frame_id, is_main_frame, security_origin),
        callback_(callback) {}

  bool CallJavaScriptFunction(
      const std::string& name,
      const std::vector<base::Value>& parameters) override {
    callback_();
    return web::FakeWebFrame::CallJavaScriptFunction(name, parameters);
  }

 private:
  std::function<void()> callback_;
};

// Test fixture for AutofillAgent testing.
class AutofillAgentTests : public PlatformTest {
 public:
  AutofillAgentTests() {}

  void SetUp() override {
    PlatformTest::SetUp();

    // Mock CRWJSInjectionReceiver for verifying interactions.
    mock_js_injection_receiver_ =
        [OCMockObject mockForClass:[CRWJSInjectionReceiver class]];
    test_web_state_.SetBrowserState(&test_browser_state_);
    test_web_state_.SetJSInjectionReceiver(mock_js_injection_receiver_);
    test_web_state_.SetContentIsHTML(true);
    GURL url("https://example.com");
    test_web_state_.SetCurrentURL(url);
    test_web_state_.CreateWebFramesManager();
    auto main_frame = std::make_unique<web::FakeWebFrame>("frameID", true, url);
    fake_main_frame_ = main_frame.get();
    test_web_state_.AddWebFrame(std::move(main_frame));

    prefs_ = autofill::test::PrefServiceForTesting();
    autofill::prefs::SetAutofillEnabled(prefs_.get(), true);
    autofill_agent_ =
        [[AutofillAgent alloc] initWithPrefService:prefs_.get()
                                          webState:&test_web_state_];
  }

  web::TestWebThreadBundle thread_bundle_;
  web::TestBrowserState test_browser_state_;
  web::TestWebState test_web_state_;
  web::FakeWebFrame* fake_main_frame_ = nullptr;
  autofill::TestAutofillClient client_;
  std::unique_ptr<PrefService> prefs_;
  AutofillAgent* autofill_agent_;
  id mock_js_injection_receiver_;

  DISALLOW_COPY_AND_ASSIGN(AutofillAgentTests);
};

// Tests that form's name and fields' identifiers, values, and whether they are
// autofilled are sent to the JS. Fields with empty values and those that are
// not autofilled are skipped.
TEST_F(AutofillAgentTests, OnFormDataFilledTestWithFrameMessaging) {
  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &test_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  autofill::FormData form;
  form.origin = GURL("https://myform.com");
  form.action = GURL("https://myform.com/submit");
  form.name = base::ASCIIToUTF16("CC form");

  autofill::FormFieldData field;
  field.form_control_type = "text";
  field.label = base::ASCIIToUTF16("Card number");
  field.name = base::ASCIIToUTF16("number");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("number");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("number_value");
  field.is_autofilled = true;
  form.fields.push_back(field);
  field.label = base::ASCIIToUTF16("Name on Card");
  field.name = base::ASCIIToUTF16("name");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("name");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("name_value");
  field.is_autofilled = true;
  form.fields.push_back(field);
  field.label = base::ASCIIToUTF16("Expiry Month");
  field.name = base::ASCIIToUTF16("expiry_month");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("expiry_month");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("01");
  field.is_autofilled = false;
  form.fields.push_back(field);
  field.label = base::ASCIIToUTF16("Unknown field");
  field.name = base::ASCIIToUTF16("unknown");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("unknown");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("");
  field.is_autofilled = true;
  form.fields.push_back(field);
  [autofill_agent_ fillFormData:form
                        inFrame:web::GetMainWebFrame(&test_web_state_)];
  test_web_state_.WasShown();
  EXPECT_EQ(
      "__gCrWeb.autofill.fillForm({\"fields\":{\"name\":{\"section\":\"\","
      "\"value\":\"name_value\"},"
      "\"number\":{\"section\":\"\",\"value\":\"number_value\"}},"
      "\"formName\":\"CC form\"}, \"\");",
      fake_main_frame_->GetLastJavaScriptCall());
}

// Tests that in the case of conflict in fields' identifiers, the last seen
// value of a given field is used.
TEST_F(AutofillAgentTests,
       OnFormDataFilledWithNameCollisionTestFrameMessaging) {
  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &test_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  autofill::FormData form;
  form.origin = GURL("https://myform.com");
  form.action = GURL("https://myform.com/submit");

  autofill::FormFieldData field;
  field.form_control_type = "text";
  field.label = base::ASCIIToUTF16("State");
  field.name = base::ASCIIToUTF16("region");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("region");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("California");
  field.is_autofilled = true;
  form.fields.push_back(field);
  field.label = base::ASCIIToUTF16("Other field");
  field.name = base::ASCIIToUTF16("field1");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("field1");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("value 1");
  field.is_autofilled = true;
  form.fields.push_back(field);
  field.label = base::ASCIIToUTF16("Other field");
  field.name = base::ASCIIToUTF16("field1");
  field.name_attribute = field.name;
  field.id_attribute = base::ASCIIToUTF16("field1");
  field.unique_id = field.id_attribute;
  field.value = base::ASCIIToUTF16("value 2");
  field.is_autofilled = true;
  form.fields.push_back(field);
  // Fields are in alphabetical order.
  [autofill_agent_ fillFormData:form
                        inFrame:web::GetMainWebFrame(&test_web_state_)];
  test_web_state_.WasShown();
  EXPECT_EQ("__gCrWeb.autofill.fillForm({\"fields\":{\"field1\":{\"section\":"
            "\"\",\"value\":\"value "
            "2\"},\"region\":{\"section\":\"\",\"value\":\"California\"}},"
            "\"formName\":\"\"}, \"\");",
            fake_main_frame_->GetLastJavaScriptCall());
}

// Tests that when a user initiated form activity is registered the script to
// extract forms is executed.
TEST_F(AutofillAgentTests,
       CheckIfSuggestionsAvailable_UserInitiatedActivity1FrameMessaging) {
  base::test::ScopedFeatureList scoped_feature_list;
  std::vector<base::Feature> enabled_features;
  std::vector<base::Feature> disabled_features;
  enabled_features.push_back(
      autofill::features::kAutofillRestrictUnownedFieldsToFormlessCheckout);
  scoped_feature_list.InitWithFeatures(enabled_features, disabled_features);
  [autofill_agent_ checkIfSuggestionsAvailableForForm:@"form"
                                      fieldIdentifier:@"address"
                                            fieldType:@"text"
                                                 type:@"focus"
                                           typedValue:@""
                                              frameID:@"frameID"
                                          isMainFrame:YES
                                       hasUserGesture:YES
                                             webState:&test_web_state_
                                    completionHandler:nil];
  test_web_state_.WasShown();
  EXPECT_EQ("__gCrWeb.autofill.extractForms(1, true);",
            fake_main_frame_->GetLastJavaScriptCall());
}

// Tests that when a non user initiated form activity is registered the
// completion callback passed to the call to check if suggestions are available
// is invoked with no suggestions.
TEST_F(AutofillAgentTests,
       CheckIfSuggestionsAvailable_NonUserInitiatedActivity) {
  __block BOOL completion_handler_success = NO;
  __block BOOL completion_handler_called = NO;

  [autofill_agent_ checkIfSuggestionsAvailableForForm:@"form"
                                      fieldIdentifier:@"address"
                                            fieldType:@"text"
                                                 type:@"focus"
                                           typedValue:@""
                                              frameID:@"frameID"
                                          isMainFrame:YES
                                       hasUserGesture:NO
                                             webState:&test_web_state_
                                    completionHandler:^(BOOL success) {
                                      completion_handler_success = success;
                                      completion_handler_called = YES;
                                    }];
  test_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });
  EXPECT_FALSE(completion_handler_success);
}

// Tests that when Autofill suggestions are made available to AutofillAgent
// "Clear Form" is moved to the start of the list and the order of other
// suggestions remains unchanged.
TEST_F(AutofillAgentTests, onSuggestionsReady_ClearForm) {
  __block NSArray<FormSuggestion*>* completion_handler_suggestions = nil;
  __block BOOL completion_handler_called = NO;

  // Make the suggestions available to AutofillAgent.
  std::vector<autofill::Suggestion> suggestions;
  suggestions.push_back(autofill::Suggestion("", "", "", 123));
  suggestions.push_back(autofill::Suggestion("", "", "", 321));
  suggestions.push_back(
      autofill::Suggestion("", "", "", POPUP_ITEM_ID_CLEAR_FORM));
  [autofill_agent_
      showAutofillPopup:suggestions
          popupDelegate:base::WeakPtr<autofill::AutofillPopupDelegate>()];

  // Retrieves the suggestions.
  auto completionHandler = ^(NSArray<FormSuggestion*>* suggestions,
                             id<FormSuggestionProvider> delegate) {
    completion_handler_suggestions = [suggestions copy];
    completion_handler_called = YES;
  };
  [autofill_agent_ retrieveSuggestionsForForm:@"form"
                              fieldIdentifier:@"address"
                                    fieldType:@"text"
                                         type:@"focus"
                                   typedValue:@""
                                      frameID:@"frameID"
                                     webState:&test_web_state_
                            completionHandler:completionHandler];
  test_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });

  // "Clear Form" should appear as the first suggestion. Otherwise, the order of
  // suggestions should not change.
  EXPECT_EQ(3U, completion_handler_suggestions.count);
  EXPECT_EQ(POPUP_ITEM_ID_CLEAR_FORM,
            completion_handler_suggestions[0].identifier);
  EXPECT_EQ(123, completion_handler_suggestions[1].identifier);
  EXPECT_EQ(321, completion_handler_suggestions[2].identifier);
}

// Tests that when Autofill suggestions are made available to AutofillAgent
// GPay icon remains as the first suggestion.
TEST_F(AutofillAgentTests, onSuggestionsReady_ClearFormWithGPay) {
  __block NSArray<FormSuggestion*>* completion_handler_suggestions = nil;
  __block BOOL completion_handler_called = NO;

  // Make the suggestions available to AutofillAgent.
  std::vector<autofill::Suggestion> suggestions;
  suggestions.push_back(
      autofill::Suggestion("", "", "", POPUP_ITEM_ID_GOOGLE_PAY_BRANDING));
  suggestions.push_back(autofill::Suggestion("", "", "", 123));
  suggestions.push_back(autofill::Suggestion("", "", "", 321));
  suggestions.push_back(
      autofill::Suggestion("", "", "", POPUP_ITEM_ID_CLEAR_FORM));
  [autofill_agent_
      showAutofillPopup:suggestions
          popupDelegate:base::WeakPtr<autofill::AutofillPopupDelegate>()];

  // Retrieves the suggestions.
  auto completionHandler = ^(NSArray<FormSuggestion*>* suggestions,
                             id<FormSuggestionProvider> delegate) {
    completion_handler_suggestions = [suggestions copy];
    completion_handler_called = YES;
  };
  [autofill_agent_ retrieveSuggestionsForForm:@"form"
                              fieldIdentifier:@"address"
                                    fieldType:@"text"
                                         type:@"focus"
                                   typedValue:@""
                                      frameID:@"frameID"
                                     webState:&test_web_state_
                            completionHandler:completionHandler];
  test_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });

  // GPay icon should appear as the first suggestion followed by "Clear Form".
  // Otherwise, the order of suggestions should not change.
  EXPECT_EQ(4U, completion_handler_suggestions.count);
  EXPECT_EQ(POPUP_ITEM_ID_GOOGLE_PAY_BRANDING,
            completion_handler_suggestions[0].identifier);
  EXPECT_EQ(POPUP_ITEM_ID_CLEAR_FORM,
            completion_handler_suggestions[1].identifier);
  EXPECT_EQ(123, completion_handler_suggestions[2].identifier);
  EXPECT_EQ(321, completion_handler_suggestions[3].identifier);
}

// Test that every frames are processed whatever is the order of pageloading
// callbacks. The main frame should always be processed first.
TEST_F(AutofillAgentTests, FrameInitializationOrderFrames) {
  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &test_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  // Remove the current main frame.
  test_web_state_.RemoveWebFrame(fake_main_frame_->GetFrameId());

  // Both frames available, then page loaded.
  test_web_state_.SetLoading(true);
  auto main_frame_unique =
      std::make_unique<web::FakeWebFrame>("main", true, GURL());
  web::FakeWebFrame* main_frame = main_frame_unique.get();
  test_web_state_.AddWebFrame(std::move(main_frame_unique));
  autofill::AutofillDriverIOS* main_frame_driver =
      autofill::AutofillDriverIOS::FromWebStateAndWebFrame(&test_web_state_,
                                                           main_frame);
  EXPECT_TRUE(main_frame_driver->IsInMainFrame());
  auto iframe_unique = std::make_unique<FakeWebFrameCallback>(
      "iframe", false, GURL(), [main_frame_driver]() {
        EXPECT_TRUE(main_frame_driver->is_processed());
      });
  FakeWebFrameCallback* iframe = iframe_unique.get();
  test_web_state_.AddWebFrame(std::move(iframe_unique));
  autofill::AutofillDriverIOS* iframe_driver =
      autofill::AutofillDriverIOS::FromWebStateAndWebFrame(&test_web_state_,
                                                           iframe);
  EXPECT_FALSE(iframe_driver->IsInMainFrame());
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.SetLoading(false);
  test_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  test_web_state_.RemoveWebFrame(main_frame->GetFrameId());
  test_web_state_.RemoveWebFrame(iframe->GetFrameId());

  // Main frame available, then page loaded, then iframe available
  main_frame_unique = std::make_unique<web::FakeWebFrame>("main", true, GURL());
  main_frame = main_frame_unique.get();
  main_frame_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &test_web_state_, main_frame);
  iframe_unique = std::make_unique<FakeWebFrameCallback>(
      "iframe", false, GURL(), [main_frame_driver]() {
        EXPECT_TRUE(main_frame_driver->is_processed());
      });
  iframe = iframe_unique.get();
  iframe_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &test_web_state_, iframe);
  test_web_state_.SetLoading(true);
  test_web_state_.AddWebFrame(std::move(main_frame_unique));
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.SetLoading(false);
  test_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.AddWebFrame(std::move(iframe_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  test_web_state_.RemoveWebFrame(main_frame->GetFrameId());
  test_web_state_.RemoveWebFrame(iframe->GetFrameId());

  // Page loaded, then main frame, then iframe
  main_frame_unique = std::make_unique<web::FakeWebFrame>("main", true, GURL());
  main_frame = main_frame_unique.get();
  main_frame_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &test_web_state_, main_frame);
  iframe_unique = std::make_unique<FakeWebFrameCallback>(
      "iframe", false, GURL(), [main_frame_driver]() {
        EXPECT_TRUE(main_frame_driver->is_processed());
      });
  iframe = iframe_unique.get();
  iframe_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &test_web_state_, iframe);
  test_web_state_.SetLoading(true);
  test_web_state_.SetLoading(false);
  test_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.AddWebFrame(std::move(main_frame_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.AddWebFrame(std::move(iframe_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  test_web_state_.RemoveWebFrame(main_frame->GetFrameId());
  test_web_state_.RemoveWebFrame(iframe->GetFrameId());

  // Page loaded, then iframe, then main frame
  main_frame_unique = std::make_unique<web::FakeWebFrame>("main", true, GURL());
  main_frame = main_frame_unique.get();
  main_frame_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &test_web_state_, main_frame);
  iframe_unique = std::make_unique<FakeWebFrameCallback>(
      "iframe", false, GURL(), [main_frame_driver]() {
        EXPECT_TRUE(main_frame_driver->is_processed());
      });
  iframe = iframe_unique.get();
  iframe_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &test_web_state_, iframe);
  test_web_state_.SetLoading(true);
  test_web_state_.SetLoading(false);
  test_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.AddWebFrame(std::move(iframe_unique));
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  test_web_state_.AddWebFrame(std::move(main_frame_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  test_web_state_.RemoveWebFrame(main_frame->GetFrameId());
  test_web_state_.RemoveWebFrame(iframe->GetFrameId());
}
