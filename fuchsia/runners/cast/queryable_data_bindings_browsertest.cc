// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "fuchsia/base/fit_adapter.h"
#include "fuchsia/base/mem_buffer_util.h"
#include "fuchsia/base/result_receiver.h"
#include "fuchsia/base/test_navigation_observer.h"
#include "fuchsia/engine/test/web_engine_browser_test.h"
#include "fuchsia/fidl/chromium/web/cpp/fidl.h"
#include "fuchsia/runners/cast/fake_queryable_data.h"
#include "fuchsia/runners/cast/named_message_port_connector.h"
#include "fuchsia/runners/cast/queryable_data_bindings.h"

namespace {

class QueryableDataBindingsTest : public cr_fuchsia::WebEngineBrowserTest {
 public:
  QueryableDataBindingsTest()
      : queryable_data_service_binding_(&queryable_data_service_) {
    set_test_server_root(base::FilePath("fuchsia/runners/cast/testdata"));
  }

  ~QueryableDataBindingsTest() override = default;

  void SetUpOnMainThread() override {
    cr_fuchsia::WebEngineBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    frame_ = WebEngineBrowserTest::CreateFrame(&navigation_observer_);

    ASSERT_TRUE(embedded_test_server()->Start());
    test_url_ = embedded_test_server()->GetURL("/query_platform_value.html");

    navigation_observer_.SetBeforeAckHook(base::BindRepeating(
        &QueryableDataBindingsTest::OnBeforeAckHook, base::Unretained(this)));

    connector_.Register(
        "testQuery",
        base::BindRepeating(&QueryableDataBindingsTest::ReceiveMessagePort,
                            base::Unretained(this)),
        frame_.get());
  }

  // Blocks test execution until the page has indicated that it's processed the
  // updates, which we achieve by setting the title to a new value and waiting
  // for the resulting navigation event.
  void SynchronizeWithPage() {
    std::string unique_title =
        base::StringPrintf("sync-%d", current_sync_id_++);
    frame_->ExecuteJavaScript(
        {"*"},
        cr_fuchsia::MemBufferFromString(
            base::StringPrintf("document.title = '%s'", unique_title.c_str())),
        chromium::web::ExecuteMode::IMMEDIATE_ONCE,
        [](bool success) { ASSERT_TRUE(success); });

    navigation_observer_.RunUntilNavigationEquals(test_url_, unique_title);
  }

  // Communicates with the page to read an entry from its QueryableData store.
  std::string CallQueryPlatformValue(base::StringPiece key) {
    // Wait until the querying MessagePort is ready to use.
    if (!query_port_) {
      base::RunLoop run_loop;
      on_query_port_received_cb_ = run_loop.QuitClosure();
      run_loop.Run();
      DCHECK(query_port_);
    }

    // Send the request to the page.
    chromium::web::WebMessage message;
    message.data = cr_fuchsia::MemBufferFromString(key);
    query_port_->PostMessage(std::move(message),
                             [](bool success) { DCHECK(success); });

    // Return the response from the page.
    base::RunLoop response_loop;
    cr_fuchsia::ResultReceiver<chromium::web::WebMessage> response(
        response_loop.QuitClosure());
    query_port_->ReceiveMessage(
        cr_fuchsia::CallbackToFitFunction(response.GetReceiveCallback()));
    response_loop.Run();
    std::string response_string;
    bool success =
        cr_fuchsia::StringFromMemBuffer(response->data, &response_string);
    CHECK(success);
    return response_string;
  }

  void ReceiveMessagePort(chromium::web::MessagePortPtr port) {
    query_port_ = std::move(port);
    if (on_query_port_received_cb_)
      std::move(on_query_port_received_cb_).Run();
  }

 protected:
  void OnBeforeAckHook(
      const chromium::web::NavigationEvent& change,
      chromium::web::NavigationEventObserver::OnNavigationStateChangedCallback
          callback) {
    if (!change.url.is_null())
      connector_.NotifyPageLoad(frame_.get());

    callback();
  }

  chromium::web::FramePtr frame_;

  GURL test_url_;
  NamedMessagePortConnector connector_;
  FakeQueryableData queryable_data_service_;
  cr_fuchsia::TestNavigationObserver navigation_observer_;
  fidl::Binding<chromium::cast::QueryableData> queryable_data_service_binding_;
  base::OnceClosure on_query_port_received_cb_;
  base::OnceClosure on_navigate_cb_;
  chromium::web::MessagePortPtr query_port_;
  int current_sync_id_ = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(QueryableDataBindingsTest);
};

IN_PROC_BROWSER_TEST_F(QueryableDataBindingsTest, VariousTypes) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  base::DictionaryValue dict_value;
  dict_value.SetString("key", "val");
  queryable_data_service_.SendChanges({{"string", base::Value("foo")},
                                       {"number", base::Value(123)},
                                       {"null", base::Value()},
                                       {"dict", std::move(dict_value)}});

  QueryableDataBindings bindings(
      frame_.get(), queryable_data_service_binding_.NewBinding().Bind());

  chromium::web::NavigationControllerPtr controller;
  frame_->GetNavigationController(controller.NewRequest());
  frame_->SetJavaScriptLogLevel(chromium::web::LogLevel::INFO);
  controller->LoadUrl(test_url_.spec(), chromium::web::LoadUrlParams());
  navigation_observer_.RunUntilNavigationEquals(test_url_, {});

  EXPECT_EQ(CallQueryPlatformValue("string"), "\"foo\"");
  EXPECT_EQ(CallQueryPlatformValue("number"), "123");
  EXPECT_EQ(CallQueryPlatformValue("null"), "null");
  EXPECT_EQ(CallQueryPlatformValue("dict"), "{\"key\":\"val\"}");
}

IN_PROC_BROWSER_TEST_F(QueryableDataBindingsTest, NoValues) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  QueryableDataBindings bindings(
      frame_.get(), queryable_data_service_binding_.NewBinding().Bind());

  chromium::web::NavigationControllerPtr controller;
  frame_->GetNavigationController(controller.NewRequest());
  frame_->SetJavaScriptLogLevel(chromium::web::LogLevel::INFO);
  controller->LoadUrl(test_url_.spec(), chromium::web::LoadUrlParams());
  navigation_observer_.RunUntilNavigationEquals(test_url_, {});

  EXPECT_EQ(CallQueryPlatformValue("string"), "null");
}

IN_PROC_BROWSER_TEST_F(QueryableDataBindingsTest, AtPageRuntime) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  queryable_data_service_.SendChanges({{"key1", base::Value(1)},
                                       {"key2", base::Value(2)},
                                       {"key3", base::Value(3)}});

  QueryableDataBindings bindings(
      frame_.get(), queryable_data_service_binding_.NewBinding().Bind());

  chromium::web::NavigationControllerPtr controller;
  frame_->GetNavigationController(controller.NewRequest());
  frame_->SetJavaScriptLogLevel(chromium::web::LogLevel::INFO);
  controller->LoadUrl(test_url_.spec(), chromium::web::LoadUrlParams());
  navigation_observer_.RunUntilNavigationEquals(test_url_, {});

  SynchronizeWithPage();

  EXPECT_EQ(CallQueryPlatformValue("key1"), "1");
  EXPECT_EQ(CallQueryPlatformValue("key2"), "2");
  EXPECT_EQ(CallQueryPlatformValue("key3"), "3");

  queryable_data_service_.SendChanges(
      {{"key1", base::Value(10)}, {"key2", base::Value(20)}});

  SynchronizeWithPage();

  // Verify that the changes are immediately available.
  EXPECT_EQ(CallQueryPlatformValue("key1"), "10");
  EXPECT_EQ(CallQueryPlatformValue("key2"), "20");
  EXPECT_EQ(CallQueryPlatformValue("key3"), "3");
}

// Sends updates to the Frame before the Frame has created a renderer.
IN_PROC_BROWSER_TEST_F(QueryableDataBindingsTest, AtPageLoad) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  queryable_data_service_.SendChanges({{"key1", base::Value(1)},
                                       {"key2", base::Value(2)},
                                       {"key3", base::Value(3)}});

  queryable_data_service_.SendChanges(
      {{"key1", base::Value(10)}, {"key2", base::Value(20)}});

  QueryableDataBindings bindings(
      frame_.get(), queryable_data_service_binding_.NewBinding().Bind());

  chromium::web::NavigationControllerPtr controller;
  frame_->GetNavigationController(controller.NewRequest());
  frame_->SetJavaScriptLogLevel(chromium::web::LogLevel::INFO);
  controller->LoadUrl(test_url_.spec(), chromium::web::LoadUrlParams());
  navigation_observer_.RunUntilNavigationEquals(test_url_, {});

  SynchronizeWithPage();

  EXPECT_EQ(CallQueryPlatformValue("key1"), "10");
  EXPECT_EQ(CallQueryPlatformValue("key2"), "20");
  EXPECT_EQ(CallQueryPlatformValue("key3"), "3");
}

}  // namespace
