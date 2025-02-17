// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_H_
#define UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_H_

#include "base/callback.h"
#include "base/lazy_instance.h"
#include "base/observer_list.h"
#include "build/build_config.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_export.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_mode_observer.h"
#include "ui/gfx/native_widget_types.h"

namespace ui {

class AXPlatformNodeDelegate;

// AXPlatformNode is the abstract base class for an implementation of
// native accessibility APIs on supported platforms (e.g. Windows, Mac OS X).
// An object that wants to be accessible can derive from AXPlatformNodeDelegate
// and then call AXPlatformNode::Create. The delegate implementation should
// own the AXPlatformNode instance (or otherwise manage its lifecycle).
class AX_EXPORT AXPlatformNode {
 public:
  using NativeWindowHandlerCallback =
      base::RepeatingCallback<AXPlatformNode*(gfx::NativeWindow)>;

  // Create an appropriate platform-specific instance. The delegate owns the
  // AXPlatformNode instance (or manages its lifecycle in some other way).
  static AXPlatformNode* Create(AXPlatformNodeDelegate* delegate);

  // Cast a gfx::NativeViewAccessible to an AXPlatformNode if it is one,
  // or return nullptr if it's not an instance of this class.
  static AXPlatformNode* FromNativeViewAccessible(
      gfx::NativeViewAccessible accessible);

  // Return the AXPlatformNode at the root of the tree for a native window.
  static AXPlatformNode* FromNativeWindow(gfx::NativeWindow native_window);

  // Provide a function that returns the AXPlatformNode at the root of the
  // tree for a native window.
  static void RegisterNativeWindowHandler(NativeWindowHandlerCallback handler);

  // Register and unregister to receive notifications about AXMode changes
  // for this node.
  static void AddAXModeObserver(AXModeObserver* observer);
  static void RemoveAXModeObserver(AXModeObserver* observer);

  // Convenience method to get the current accessibility mode.
  static AXMode GetAccessibilityMode() { return ax_mode_; }

  // Helper static function to notify all global observers about
  // the addition of an AXMode flag.
  static void NotifyAddAXModeFlags(AXMode mode_flags);

  // Must be called by native suggestion code when there are suggestions which
  // could be presented in a popup, even if the popup is not presently visible.
  // The availability of the popup changes the interactions that will occur
  // (down arrow will move the focus into the suggestion popup). An example of a
  // suggestion popup is seen in the Autofill feature.
  // TODO(crbug.com/865101) Remove this once the autofill state works.
  static void OnInputSuggestionsAvailable();
  // Must be called when the system goes from a state of having an available
  // suggestion popup to none available. If the suggestion popup is still
  // available but just hidden, this method should not be called.
  // TODO(crbug.com/865101) Remove this once the autofill state works.
  static void OnInputSuggestionsUnavailable();

  // TODO(crbug.com/865101) Remove this once the autofill state works.
  static bool HasInputSuggestions();

  // Return the focused object in any UI popup overlaying content, or null.
  static gfx::NativeViewAccessible GetPopupFocusOverride();

  // Set the focused object withn any UI popup overlaying content, or null.
  // The focus override is the perceived focus within the popup, and it changes
  // each time a user navigates to a new item within the popup.
  static void SetPopupFocusOverride(gfx::NativeViewAccessible focus_override);

  // Call Destroy rather than deleting this, because the subclass may
  // use reference counting.
  virtual void Destroy();

  // Get the platform-specific accessible object type for this instance.
  // On some platforms this is just a type cast, on others it may be a
  // wrapper object or handle.
  virtual gfx::NativeViewAccessible GetNativeViewAccessible() = 0;

  // Fire a platform-specific notification that an event has occurred on
  // this object.
  virtual void NotifyAccessibilityEvent(ax::mojom::Event event_type) = 0;

#if defined(OS_MACOSX)
  // Fire a platform-specific notification to announce |text|.
  virtual void AnnounceText(base::string16& text) = 0;
#endif

  // Return this object's delegate.
  virtual AXPlatformNodeDelegate* GetDelegate() const = 0;

  // Return true if this object is equal to or a descendant of |ancestor|.
  virtual bool IsDescendantOf(AXPlatformNode* ancestor) const = 0;

  // Return the unique ID
  int32_t GetUniqueId() const;

 protected:
  AXPlatformNode();
  virtual ~AXPlatformNode();

 private:
  // Global ObserverList for AXMode changes.
  static base::LazyInstance<
      base::ObserverList<AXModeObserver>::Unchecked>::Leaky ax_mode_observers_;

  static base::LazyInstance<NativeWindowHandlerCallback>::Leaky
      native_window_handler_;

  static AXMode ax_mode_;

  static bool has_input_suggestions_;

  // This allows UI menu popups like to act as if they are focused in the
  // exposed platform accessibility API, even though actual focus remains in
  // underlying content.
  static gfx::NativeViewAccessible popup_focus_override_;

  DISALLOW_COPY_AND_ASSIGN(AXPlatformNode);
};

}  // namespace ui

#endif  // UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_H_
