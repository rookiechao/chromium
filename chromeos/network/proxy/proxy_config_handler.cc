// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/proxy/proxy_config_handler.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill/shill_service_client.h"
#include "chromeos/network/network_handler_callbacks.h"
#include "chromeos/network/network_profile.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/onc/onc_utils.h"
#include "components/onc/onc_pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "dbus/object_path.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

void NotifyNetworkStateHandler(const std::string& service_path) {
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RequestUpdateForNetwork(
        service_path);
  }
}

}  // namespace

namespace proxy_config {

std::unique_ptr<ProxyConfigDictionary> GetProxyConfigForNetwork(
    const PrefService* profile_prefs,
    const PrefService* local_state_prefs,
    const NetworkState& network,
    ::onc::ONCSource* onc_source) {
  const base::DictionaryValue* network_policy = onc::GetPolicyForNetwork(
      profile_prefs, local_state_prefs, network, onc_source);

  if (network_policy) {
    const base::DictionaryValue* proxy_policy = NULL;
    network_policy->GetDictionaryWithoutPathExpansion(
        ::onc::network_config::kProxySettings, &proxy_policy);
    if (!proxy_policy) {
      // This policy doesn't set a proxy for this network. Nonetheless, this
      // disallows changes by the user.
      return nullptr;
    }

    return std::make_unique<ProxyConfigDictionary>(
        onc::ConvertOncProxySettingsToProxyConfig(*proxy_policy));
  }

  if (network.profile_path().empty())
    return nullptr;

  const NetworkProfile* profile =
      NetworkHandler::Get()->network_profile_handler()->GetProfileForPath(
          network.profile_path());
  if (!profile) {
    VLOG(1) << "Unknown profile_path '" << network.profile_path() << "'.";
    return nullptr;
  }
  if (!profile_prefs && profile->type() == NetworkProfile::TYPE_USER) {
    // This case occurs, for example, if called from the proxy config tracker
    // created for the system request context and the signin screen. Both don't
    // use profile prefs and shouldn't depend on the user's not shared proxy
    // settings.
    VLOG(1)
        << "Don't use unshared settings for system context or signin screen.";
    return nullptr;
  }

  // No policy set for this network, read instead the user's (shared or
  // unshared) configuration.
  // The user's proxy setting is not stored in the Chrome preference yet. We
  // still rely on Shill storing it.
  const base::Value* value = network.proxy_config();
  if (!value)
    return nullptr;
  return std::make_unique<ProxyConfigDictionary>(value->Clone());
}

void SetProxyConfigForNetwork(const ProxyConfigDictionary& proxy_config,
                              const NetworkState& network) {
  chromeos::ShillServiceClient* shill_service_client =
      DBusThreadManager::Get()->GetShillServiceClient();

  // The user's proxy setting is not stored in the Chrome preference yet. We
  // still rely on Shill storing it.
  ProxyPrefs::ProxyMode mode;
  if (!proxy_config.GetMode(&mode) || mode == ProxyPrefs::MODE_DIRECT) {
    // Return empty string for direct mode for portal check to work correctly.
    // TODO(pneubeck): Consider removing this legacy code.
    shill_service_client->ClearProperty(
        dbus::ObjectPath(network.path()), shill::kProxyConfigProperty,
        base::Bind(&NotifyNetworkStateHandler, network.path()),
        base::Bind(&network_handler::ShillErrorCallbackFunction,
                   "SetProxyConfig.ClearProperty Failed", network.path(),
                   network_handler::ErrorCallback()));
  } else {
    std::string proxy_config_str;
    base::JSONWriter::Write(proxy_config.GetDictionary(), &proxy_config_str);
    shill_service_client->SetProperty(
        dbus::ObjectPath(network.path()), shill::kProxyConfigProperty,
        base::Value(proxy_config_str),
        base::Bind(&NotifyNetworkStateHandler, network.path()),
        base::Bind(&network_handler::ShillErrorCallbackFunction,
                   "SetProxyConfig.SetProperty Failed", network.path(),
                   network_handler::ErrorCallback()));
  }
}

}  // namespace proxy_config

}  // namespace chromeos
