// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_BATCH_ELEMENT_CHECKER_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_BATCH_ELEMENT_CHECKER_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill_assistant/browser/selector.h"

namespace autofill_assistant {
class WebController;

// Types of element checks.
enum ElementCheckType { kExistenceCheck, kVisibilityCheck };

// Helper for checking a set of elements at the same time. It avoids duplicate
// checks.
class BatchElementChecker {
 public:
  explicit BatchElementChecker();
  virtual ~BatchElementChecker();

  // Callback for AddElementCheck. Argument is true if the check passed.
  //
  // An ElementCheckCallback must not delete its calling BatchElementChecker.
  using ElementCheckCallback = base::OnceCallback<void(bool)>;

  // Callback for AddFieldValueCheck. Argument is true is the element exists.
  // The string contains the field value, or an empty string if accessing the
  // value failed.
  //
  // An ElementCheckCallback must not delete its calling BatchElementChecker.
  using GetFieldValueCallback =
      base::OnceCallback<void(bool, const std::string&)>;

  // Checks an an element.
  //
  // kElementCheck checks whether at least one element given by |selector|
  // exists on the web page.
  //
  // kVisibilityCheck checks whether at least one element given by |selector|
  // is visible on the page.
  //
  // New element checks cannot be added once Run has been called.
  void AddElementCheck(ElementCheckType check_type,
                       const Selector& selector,
                       ElementCheckCallback callback);

  // Gets the value of |selector| and return the result through |callback|. The
  // returned value will be the empty string in case of error or empty value.
  //
  // New field checks cannot be added once Run has been called.
  void AddFieldValueCheck(const Selector& selector,
                          GetFieldValueCallback callback);

  // Returns true if all there are no checks to run.
  bool empty() const;

  // Runs the checks. Call |all_done| once all the results have been reported.
  void Run(WebController* web_controller, base::OnceCallback<void()> all_done);

 private:
  void OnElementChecked(std::vector<ElementCheckCallback>* callbacks,
                        bool exists);
  void OnGetFieldValue(std::vector<GetFieldValueCallback>* callbacks,
                       bool exists,
                       const std::string& value);
  void CheckDone();

  // A map of ElementCheck arguments (check_type, selector) to callbacks that
  // take the result of the check.
  std::map<std::pair<ElementCheckType, Selector>,
           std::vector<ElementCheckCallback>>
      element_check_callbacks_;

  // A map of GetFieldValue arguments (selector) to callbacks that take the
  // field value.
  std::map<Selector, std::vector<GetFieldValueCallback>>
      get_field_value_callbacks_;
  int pending_checks_count_ = 0;

  // Run() was called. Checking elements might or might not have finished yet.
  bool started_ = false;

  base::OnceCallback<void()> all_done_;

  base::WeakPtrFactory<BatchElementChecker> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BatchElementChecker);
};

}  // namespace autofill_assistant
#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_BATCH_ELEMENT_CHECKER_H_
