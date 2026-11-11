// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_FACTORY_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace aerie_tab_model {

class SleepController;

// One SleepController per profile that has a Tab Model service. It is not
// created with the profile (issue #179, no startup cost): the first caller
// builds it. The sidebar asks for it a few seconds after its window opens.
class SleepControllerFactory : public ProfileKeyedServiceFactory {
 public:
  // Builds the controller on first use.
  static SleepController* GetForProfile(Profile* profile);
  // Null until something has asked for it.
  static SleepController* GetForProfileIfExists(Profile* profile);
  static SleepControllerFactory* GetInstance();

 private:
  friend base::NoDestructor<SleepControllerFactory>;

  SleepControllerFactory();
  ~SleepControllerFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
  bool ServiceIsNULLWhileTesting() const override;
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_FACTORY_H_
