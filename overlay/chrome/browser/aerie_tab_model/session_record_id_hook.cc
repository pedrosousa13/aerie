// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/session_record_id_hook.h"

#include <memory>
#include <utility>

#include "base/supports_user_data.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "components/aerie_tab_model/session_record_id.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace aerie_tab_model {

namespace {

const char kRestoredRecordIdUserDataKey[] = "aerie_restored_record_id";

class RestoredRecordId : public base::SupportsUserData::Data {
 public:
  explicit RestoredRecordId(std::string id) : id_(std::move(id)) {}
  const std::string& id() const { return id_; }

 private:
  std::string id_;
};

}  // namespace

void StashRestoredRecordId(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kSessionRecordIdKey);
  if (!contents || it == extra_data.end()) {
    return;
  }
  // From disk: only a well-formed id is kept.
  std::optional<std::string> id = ParseSessionRecordId(it->second);
  if (!id) {
    return;
  }
  contents->SetUserData(&kRestoredRecordIdUserDataKey,
                        std::make_unique<RestoredRecordId>(std::move(*id)));
}

std::string TakeRestoredRecordId(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }
  auto* data = static_cast<RestoredRecordId*>(
      contents->GetUserData(&kRestoredRecordIdUserDataKey));
  if (!data) {
    return std::string();
  }
  std::string id = data->id();
  contents->RemoveUserData(&kRestoredRecordIdUserDataKey);
  return id;
}

std::string RecordIdToSave(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  TabModelService* service =
      profile ? TabModelServiceFactory::GetForProfile(profile) : nullptr;
  if (!service || !service->adapter()) {
    return std::string();
  }
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  if (!tab) {
    return std::string();
  }
  return service->adapter()->SessionRecordIdForTab(tab);
}

}  // namespace aerie_tab_model
