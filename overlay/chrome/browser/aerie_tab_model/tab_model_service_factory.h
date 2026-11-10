// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_MODEL_SERVICE_FACTORY_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_MODEL_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace aerie_tab_model {

class TabModelService;

// One TabModelService per regular profile, and a separate in-memory one for
// each off-the-record profile (Guest and system profiles get none). Built when
// the profile is created (so the model is bound to the tab strips from the
// first window), and read from disk off the UI sequence.
class TabModelServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static TabModelService* GetForProfile(Profile* profile);
  static TabModelServiceFactory* GetInstance();

 private:
  friend base::NoDestructor<TabModelServiceFactory>;

  TabModelServiceFactory();
  ~TabModelServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
  bool ServiceIsNULLWhileTesting() const override;
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_MODEL_SERVICE_FACTORY_H_
