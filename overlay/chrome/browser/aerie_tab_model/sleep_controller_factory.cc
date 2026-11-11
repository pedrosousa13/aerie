// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"

#include "base/time/default_tick_clock.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace aerie_tab_model {

// static
SleepController* SleepControllerFactory::GetForProfile(Profile* profile) {
  return static_cast<SleepController*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
SleepController* SleepControllerFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<SleepController*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

// static
SleepControllerFactory* SleepControllerFactory::GetInstance() {
  static base::NoDestructor<SleepControllerFactory> instance;
  return instance.get();
}

SleepControllerFactory::SleepControllerFactory()
    : ProfileKeyedServiceFactory(
          "AerieSleepController",
          // The same as the Tab Model service: in ProfileSelections an
          // off-the-record profile takes the Regular value, so kOwnInstance
          // gives a regular profile one controller and each off-the-record
          // profile its own, which works on that profile's own in-memory Tab
          // Model. Incognito tabs sleep too. Guest and system profiles default
          // to kNone and get none.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {
  DependsOn(TabModelServiceFactory::GetInstance());
}

SleepControllerFactory::~SleepControllerFactory() = default;

std::unique_ptr<KeyedService>
SleepControllerFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  TabModelService* service = TabModelServiceFactory::GetForProfile(profile);
  if (!service) {
    return nullptr;
  }
  return std::make_unique<SleepController>(
      profile, service, base::DefaultTickClock::GetInstance());
}

bool SleepControllerFactory::ServiceIsCreatedWithBrowserContext() const {
  // Lazily: nothing is built at startup.
  return false;
}

bool SleepControllerFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace aerie_tab_model
