// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_MODEL_SERVICE_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_MODEL_SERVICE_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_persistence.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace aerie_tab_model {

class TabStripAdapter;

// The Tab Model of one profile (issue #176). The sidebar observes `model()`;
// the settings it reads (archive and sleep delays) are the aerie.tabs.* prefs.
//
// The service starts the read of AerieTabModel/state.json and binds the model to
// the profile's tab strips. The constructor only posts the read to the thread
// pool, so profile creation does not wait for the disk, and the model works
// (empty) until the read is merged. An off-the-record profile gets a model that
// stays in memory.
class TabModelService : public KeyedService {
 public:
  explicit TabModelService(Profile* profile);
  TabModelService(const TabModelService&) = delete;
  TabModelService& operator=(const TabModelService&) = delete;
  ~TabModelService() override;

  TabModel* model() { return &model_; }
  bool loaded() const { return persistence_.loaded(); }
  // False for an off-the-record profile: its model stays in memory.
  bool persists_to_disk() const { return persistence_.persists_to_disk(); }
  TabStripAdapter* adapter() { return adapter_.get(); }

  // nullopt means never.
  std::optional<base::TimeDelta> archive_after() const;
  std::optional<base::TimeDelta> sleep_after() const;

  // KeyedService:
  void Shutdown() override;

 private:
  const raw_ptr<Profile> profile_;
  // The members below are destroyed in reverse order: the adapter first, then
  // the persistence (which writes what is pending), then the model.
  TabModel model_;
  TabModelPersistence persistence_;
  std::unique_ptr<TabStripAdapter> adapter_;
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_MODEL_SERVICE_H_
