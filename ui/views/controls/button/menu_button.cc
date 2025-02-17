// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/menu_button.h"

#include <memory>

#include "ui/events/event.h"
#include "ui/views/controls/button/button_controller_delegate.h"
#include "ui/views/controls/button/menu_button_controller.h"

namespace views {
// static
const char MenuButton::kViewClassName[] = "MenuButton";

MenuButton::MenuButton(const base::string16& text,
                       MenuButtonListener* menu_button_listener,
                       int button_context)
    : LabelButton(nullptr, text, button_context) {
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetButtonController(std::make_unique<MenuButtonController>(
      this, menu_button_listener, CreateButtonControllerDelegate()));
}
MenuButton::~MenuButton() = default;

MenuButtonController* MenuButton::button_controller() const {
  return Button::button_controller()->AsMenuButtonController();
}

bool MenuButton::Activate(const ui::Event* event) {
  return button_controller()->Activate(event);
}

bool MenuButton::IsTriggerableEventType(const ui::Event& event) {
  return button_controller()->IsTriggerableEventType(event);
}

const char* MenuButton::GetClassName() const {
  return kViewClassName;
}

void MenuButton::OnGestureEvent(ui::GestureEvent* event) {
  if (button_controller()->OnGestureEvent(event))
    LabelButton::OnGestureEvent(event);
}

void MenuButton::NotifyClick(const ui::Event& event) {
  // Notify MenuButtonListener via MenuButtonController, instead of
  // ButtonListener::ButtonPressed.
  button_controller()->Activate(&event);
}

}  // namespace views
