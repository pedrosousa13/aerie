// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"

#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/profiles/profile.h"

namespace aerie_tab_model {

// static
TabModelService* TabModelServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<TabModelService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
TabModelServiceFactory* TabModelServiceFactory::GetInstance() {
  static base::NoDestructor<TabModelServiceFactory> instance;
  return instance.get();
}

TabModelServiceFactory::TabModelServiceFactory()
    : ProfileKeyedServiceFactory(
          "AerieTabModelService",
          // A regular profile and, separately, an off-the-record one. Guest
          // and system profiles get none.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {}

TabModelServiceFactory::~TabModelServiceFactory() = default;

std::unique_ptr<KeyedService>
TabModelServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<TabModelService>(Profile::FromBrowserContext(context));
}

bool TabModelServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

bool TabModelServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace aerie_tab_model
