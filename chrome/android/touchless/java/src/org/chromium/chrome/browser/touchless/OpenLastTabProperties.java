// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touchless;

import android.graphics.Bitmap;
import android.view.View;

import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

class OpenLastTabProperties {
    public static final PropertyModel
            .WritableObjectPropertyKey<View.OnClickListener> OPEN_LAST_TAB_ON_CLICK_LISTENER =
            new PropertyModel.WritableObjectPropertyKey<>();

    public static final PropertyModel.WritableObjectPropertyKey<Bitmap> OPEN_LAST_TAB_FAVICON =
            new PropertyModel.WritableObjectPropertyKey<>();

    public static final PropertyModel.WritableObjectPropertyKey<String> OPEN_LAST_TAB_TITLE =
            new PropertyModel.WritableObjectPropertyKey<>();

    public static final PropertyModel.WritableObjectPropertyKey<String> OPEN_LAST_TAB_TIMESTAMP =
            new PropertyModel.WritableObjectPropertyKey<>();

    public static final PropertyModel.WritableBooleanPropertyKey OPEN_LAST_TAB_LOAD_SUCCESS =
            new PropertyModel.WritableBooleanPropertyKey();

    // Property keys for the open last tab button.
    public static final PropertyKey[] ALL_KEYS = {OPEN_LAST_TAB_ON_CLICK_LISTENER,
            OPEN_LAST_TAB_FAVICON, OPEN_LAST_TAB_TITLE, OPEN_LAST_TAB_TIMESTAMP,
            OPEN_LAST_TAB_LOAD_SUCCESS};
}
