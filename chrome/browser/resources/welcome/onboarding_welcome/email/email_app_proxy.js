// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('nux', function() {
  /** @implements {nux.AppProxy} */
  class EmailAppProxyImpl {
    /** @override */
    cacheBookmarkIcon(emailProviderId) {
      chrome.send('cacheEmailIcon', [emailProviderId]);
    }

    /** @override */
    getAppList() {
      return cr.sendWithPromise('getEmailList');
    }

    /** @override */
    recordProviderSelected(providerId) {
      chrome.metricsPrivate.recordEnumerationValue(
          'FirstRun.NewUserExperience.EmailProvidersSelection', providerId,
          loadTimeData.getInteger('email_providers_enum_count'));
    }
  }

  cr.addSingletonGetter(EmailAppProxyImpl);

  return {
    EmailAppProxyImpl: EmailAppProxyImpl,
  };
});
