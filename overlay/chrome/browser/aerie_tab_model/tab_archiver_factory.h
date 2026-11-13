// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ARCHIVER_FACTORY_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ARCHIVER_FACTORY_H_

#include "base/no_destructor.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace aerie_tab_model {

class TabArchiver;

// One TabArchiver per profile that has a Tab Model service. It is not
// created with the profile (issue #183, no startup cost): the first caller
// builds it. The sidebar asks for it a few seconds after its window opens.
class TabArchiverFactory : public ProfileKeyedServiceFactory {
 public:
  // Builds the controller on first use.
  static TabArchiver* GetForProfile(Profile* profile);
  // Null until something has asked for it.
  static TabArchiver* GetForProfileIfExists(Profile* profile);
  // Tests: the clocks that the next archivers are built with. The wall clock is
  // the one the records' times use; the tick clock drives the timer (a test
  // that moves it calls ArchiveSchedule::RunDueNow itself). Pass null to go
  // back to the real ones.
  static void SetClocksForTesting(const base::Clock* clock,
                                  const base::TickClock* tick_clock);
  static TabArchiverFactory* GetInstance();

 private:
  friend base::NoDestructor<TabArchiverFactory>;

  TabArchiverFactory();
  ~TabArchiverFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
  bool ServiceIsNULLWhileTesting() const override;
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ARCHIVER_FACTORY_H_
