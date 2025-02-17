// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_SETTINGS_GOOGLE_SERVICES_ADVANCED_SIGNIN_SETTINGS_COORDINATOR_H_
#define IOS_CHROME_BROWSER_UI_SETTINGS_GOOGLE_SERVICES_ADVANCED_SIGNIN_SETTINGS_COORDINATOR_H_

#import "ios/chrome/browser/ui/coordinators/chrome_coordinator.h"

@class AdvancedSigninSettingsCoordinator;

// AdvancedSigninSettingsCoordinator delegate.
@protocol AdvancedSigninSettingsCoordinatorDelegate <NSObject>

// Called when the user closes AdvancedSigninSettingsCoordinator.
- (void)advancedSigninSettingsCoordinatorDidClose:
            (AdvancedSigninSettingsCoordinator*)coordinator
                                          success:(BOOL)success;

@end

// Shows the Google services settings in a navigation controller, so the user
// can update their sync settings before sync starts (or sign-out).
@interface AdvancedSigninSettingsCoordinator : ChromeCoordinator

// Delegate.
@property(nonatomic, weak) id<AdvancedSigninSettingsCoordinatorDelegate>
    delegate;

// Cancels the coordinator, and calls the delegate. This method does nothing if
// called twice.
// |dismiss|, dismisses the view controller if YES.
- (void)cancelWithDismiss:(BOOL)dismiss;

@end

#endif  // IOS_CHROME_BROWSER_UI_SETTINGS_GOOGLE_SERVICES_ADVANCED_SIGNIN_SETTINGS_COORDINATOR_H_
