// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_archiver_factory.h"

#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "chrome/browser/aerie_tab_model/tab_archiver.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace aerie_tab_model {

namespace {
const base::Clock* g_test_clock = nullptr;
const base::TickClock* g_test_tick_clock = nullptr;
}  // namespace

// static
void TabArchiverFactory::SetClocksForTesting(
    const base::Clock* clock,
    const base::TickClock* tick_clock) {
  g_test_clock = clock;
  g_test_tick_clock = tick_clock;
}

// static
TabArchiver* TabArchiverFactory::GetForProfile(Profile* profile) {
  return static_cast<TabArchiver*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
TabArchiver* TabArchiverFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<TabArchiver*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

// static
TabArchiverFactory* TabArchiverFactory::GetInstance() {
  static base::NoDestructor<TabArchiverFactory> instance;
  return instance.get();
}

TabArchiverFactory::TabArchiverFactory()
    : ProfileKeyedServiceFactory(
          "AerieTabArchiver",
          // The same as the Tab Model service: in ProfileSelections an
          // off-the-record profile takes the Regular value, so kOwnInstance
          // gives a regular profile one controller and each off-the-record
          // profile its own, which works on that profile's own in-memory Tab
          // Model. Incognito tabs archive too, in memory. Guest and system profiles default
          // to kNone and get none.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {
  DependsOn(TabModelServiceFactory::GetInstance());
}

TabArchiverFactory::~TabArchiverFactory() = default;

std::unique_ptr<KeyedService>
TabArchiverFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  TabModelService* service = TabModelServiceFactory::GetForProfile(profile);
  if (!service) {
    return nullptr;
  }
  return std::make_unique<TabArchiver>(
      profile, service,
      g_test_clock ? g_test_clock : base::DefaultClock::GetInstance(),
      g_test_tick_clock ? g_test_tick_clock
                        : base::DefaultTickClock::GetInstance());
}

bool TabArchiverFactory::ServiceIsCreatedWithBrowserContext() const {
  // Lazily: nothing is built at startup.
  return false;
}

bool TabArchiverFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace aerie_tab_model
