// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_accessible_name.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

namespace aerie_tab_model {

std::u16string AccessibleTabName(const tabs::TabInterface* tab,
                                 const std::u16string& title) {
  content::WebContents* contents = tab->GetContents();
  Profile* profile =
      contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
               : nullptr;
  if (!profile) {
    return title;
  }
  TabModelService* service =
      TabModelServiceFactory::GetForProfile(profile);
  if (!service || !service->adapter()) {
    return title;
  }
  const std::string id = service->adapter()->RecordIdForTab(tab);
  const TabRecord* record = id.empty() ? nullptr : service->model()->GetTab(id);
  if (!record) {
    return title;
  }
  // The same words, in the same order, as AerieSidebarRowView::AccessibleName:
  // the shown title, then asleep, then renamed.
  const bool renamed = record->manual_name && !record->manual_name->empty();
  std::u16string name =
      renamed ? base::UTF8ToUTF16(*record->manual_name) : title;
  if (record->sleeping) {
    name = l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_ASLEEP_NAME, name);
  }
  if (renamed) {
    name = l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_RENAMED_NAME, name);
  }
  return name;
}

}  // namespace aerie_tab_model
