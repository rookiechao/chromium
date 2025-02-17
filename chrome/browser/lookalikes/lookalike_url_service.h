// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LOOKALIKES_LOOKALIKE_URL_SERVICE_H_
#define CHROME_BROWSER_LOOKALIKES_LOOKALIKE_URL_SERVICE_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/engagement/site_engagement_details.mojom.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/url_formatter/url_formatter.h"
#include "url/gurl.h"

class GURL;
class Profile;

namespace base {
class Clock;
}

namespace lookalikes {

struct DomainInfo {
  // eTLD+1, used for skeleton and edit distance comparison. Must be ASCII.
  const std::string domain_and_registry;
  // Full hostname, used for suggesting a new URL. Must be ASCII.
  const std::string full_domain;

  // Result of IDN conversion of domain_and_registry field.
  const url_formatter::IDNConversionResult idn_result;
  // Skeletons of domain_and_registry field.
  const url_formatter::Skeletons skeletons;

  DomainInfo(const std::string& arg_domain_and_registry,
             const std::string& full_domain,
             const url_formatter::IDNConversionResult& arg_idn_result,
             const url_formatter::Skeletons& arg_skeletons);
  ~DomainInfo();
  DomainInfo(const DomainInfo& other);
};

DomainInfo GetDomainInfo(const GURL& url);

// A service that handles operations on lookalike URLs. It can fetch the list of
// engaged sites in a background thread and cache the results until the next
// update. This is more efficient than fetching the list on each navigation for
// each tab separately.
class LookalikeUrlService : public KeyedService {
 public:
  explicit LookalikeUrlService(Profile* profile);
  ~LookalikeUrlService() override;

  using EngagedSitesCallback =
      base::OnceCallback<void(const std::vector<DomainInfo>&)>;

  static LookalikeUrlService* Get(Profile* profile);

  // Checks whether the engaged site list is recently updated, and triggers
  // an update to the list if not. This method will not update the contents of
  // engaged_sites nor call |callback| if an update is not required.  The method
  // returns whether or not an update was triggered (and thus whether the
  // callback will be called).
  bool UpdateEngagedSites(EngagedSitesCallback callback);

  // Returns the _current_ list of engaged sites, without updating them if
  // they're out of date.
  const std::vector<DomainInfo> GetLatestEngagedSites() const;

  void SetClockForTesting(base::Clock* clock);

 private:
  void OnFetchEngagedSites(EngagedSitesCallback callback,
                           std::vector<mojom::SiteEngagementDetails> details);

  Profile* profile_;
  base::Clock* clock_;
  base::Time last_engagement_fetch_time_;
  std::vector<DomainInfo> engaged_sites_;
  base::WeakPtrFactory<LookalikeUrlService> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(LookalikeUrlService);
};

}  // namespace lookalikes

#endif  // CHROME_BROWSER_LOOKALIKES_LOOKALIKE_URL_SERVICE_H_
