// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/prefs/pref_notifier_impl.h"

#include "base/debug/alias.h"
#include "base/debug/dump_without_crashing.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "components/prefs/pref_service.h"

PrefNotifierImpl::PrefNotifierImpl() : pref_service_(nullptr) {}

PrefNotifierImpl::PrefNotifierImpl(PrefService* service)
    : pref_service_(service) {
}

PrefNotifierImpl::~PrefNotifierImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Verify that there are no pref observers when we shut down.
  for (const auto& observer_list : pref_observers_) {
    if (observer_list.second->begin() != observer_list.second->end()) {
      // Generally, there should not be any subscribers left when the profile
      // is destroyed because a) those may indicate that the subscriber class
      // maintains an active pointer to the profile that might be used for
      // accessing a destroyed profile and b) those subscribers will try to
      // unsubscribe from a PrefService that has been destroyed with the
      // profile.
      // There is one exception that is safe: Static objects that are leaked
      // on process termination, if these objects just subscribe to preferences
      // and never access the profile after destruction. As these objects are
      // leaked on termination, it is guaranteed that they don't attempt to
      // unsubscribe.
      const auto& pref_name = observer_list.first;
      LOG(WARNING) << "Pref observer for " << pref_name
                   << " found at shutdown.";

      // TODO(crbug.com/942491, 946668, 945772) The following code collects
      // stacktraces that show how the profile is destroyed that owns
      // preferences which are known to have subscriptions outliving the
      // profile.
      if (
          // For GlobalMenuBarX11, crbug.com/946668
          pref_name == "bookmark_bar.show_on_all_tabs" ||
          // For BrowserWindowPropertyManager, crbug.com/942491
          pref_name == "profile.icon_version" ||
          // For BrowserWindowDefaultTouchBar, crbug.com/945772
          pref_name == "default_search_provider_data.template_url_data") {
        const bool is_incognito_profile =
            pref_service_->HasInMemoryUserPrefStore();
        base::debug::Alias(&is_incognito_profile);
        // Export value of is_incognito_profile to a string so that `grep`
        // is a sufficient tool to analyze crashdumps.
        char is_incognito_profile_string[32];
        strncpy(is_incognito_profile_string,
                is_incognito_profile ? "is_incognito: yes" : "is_incognito: no",
                sizeof(is_incognito_profile_string));
        base::debug::Alias(&is_incognito_profile_string);
        base::debug::DumpWithoutCrashing();
      }
    }
  }

  // Same for initialization observers.
  if (!init_observers_.empty())
    LOG(WARNING) << "Init observer found at shutdown.";

  pref_observers_.clear();
  init_observers_.clear();
}

void PrefNotifierImpl::AddPrefObserver(const std::string& path,
                                       PrefObserver* obs) {
  // Get the pref observer list associated with the path.
  PrefObserverList* observer_list = nullptr;
  auto observer_iterator = pref_observers_.find(path);
  if (observer_iterator == pref_observers_.end()) {
    observer_list = new PrefObserverList;
    pref_observers_[path] = base::WrapUnique(observer_list);
  } else {
    observer_list = observer_iterator->second.get();
  }

  // Add the pref observer. ObserverList will DCHECK if it already is
  // in the list.
  observer_list->AddObserver(obs);
}

void PrefNotifierImpl::RemovePrefObserver(const std::string& path,
                                          PrefObserver* obs) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto observer_iterator = pref_observers_.find(path);
  if (observer_iterator == pref_observers_.end()) {
    return;
  }

  PrefObserverList* observer_list = observer_iterator->second.get();
  observer_list->RemoveObserver(obs);
}

void PrefNotifierImpl::AddPrefObserverAllPrefs(PrefObserver* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  all_prefs_pref_observers_.AddObserver(observer);
}

void PrefNotifierImpl::RemovePrefObserverAllPrefs(PrefObserver* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  all_prefs_pref_observers_.RemoveObserver(observer);
}

void PrefNotifierImpl::AddInitObserver(base::OnceCallback<void(bool)> obs) {
  init_observers_.push_back(std::move(obs));
}

void PrefNotifierImpl::OnPreferenceChanged(const std::string& path) {
  FireObservers(path);
}

void PrefNotifierImpl::OnInitializationCompleted(bool succeeded) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // We must move init_observers_ to a local variable before we run
  // observers, or we can end up in this method re-entrantly before
  // clearing the observers list.
  PrefInitObserverList observers;
  std::swap(observers, init_observers_);

  for (auto& observer : observers)
    std::move(observer).Run(succeeded);
}

void PrefNotifierImpl::FireObservers(const std::string& path) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Only send notifications for registered preferences.
  if (!pref_service_->FindPreference(path))
    return;

  // Fire observers for any preference change.
  for (auto& observer : all_prefs_pref_observers_)
    observer.OnPreferenceChanged(pref_service_, path);

  auto observer_iterator = pref_observers_.find(path);
  if (observer_iterator == pref_observers_.end())
    return;

  for (PrefObserver& observer : *(observer_iterator->second))
    observer.OnPreferenceChanged(pref_service_, path);
}

void PrefNotifierImpl::SetPrefService(PrefService* pref_service) {
  DCHECK(pref_service_ == nullptr);
  pref_service_ = pref_service;
}
