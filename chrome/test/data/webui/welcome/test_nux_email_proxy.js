// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @implements {nux.AppProxy} */
class TestNuxEmailProxy extends TestBrowserProxy {
  constructor() {
    super([
      'cacheBookmarkIcon',
      'getAppList',
      'recordProviderSelected',
    ]);

    /** @private {!Array<!nux.BookmarkListItem>} */
    this.emailList_ = [];
  }

  /** @override */
  getAppList() {
    this.methodCalled('getAppList');
    return Promise.resolve(this.emailList_);
  }

  /** @override */
  cacheBookmarkIcon() {
    this.methodCalled('cacheBookmarkIcon');
  }

  /** @override */
  recordProviderSelected(providerId) {
    this.methodCalled('recordProviderSelected', providerId);
  }

  /** @param {!Array<!nux.BookmarkListItem>} emailList */
  setEmailList(emailList) {
    this.emailList_ = emailList;
  }
}
