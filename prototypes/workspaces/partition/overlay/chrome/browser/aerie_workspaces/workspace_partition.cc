// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Prototype for issue #72, option 2. Not built. Written against Chromium 157
// (Helium 0.21.1.1); every Chromium call below was checked by reading the tree.

#include "chrome/browser/aerie_workspaces/workspace_partition.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"

namespace aerie_workspaces {

namespace {
// Same alphabet Brave allows for a container id.
bool IsValidId(std::string_view id) {
  return !id.empty() && std::ranges::all_of(id, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '-';
  });
}
}  // namespace

content::StoragePartitionConfig GetWorkspacePartitionConfig(
    content::BrowserContext* context,
    std::string_view workspace_id) {
  CHECK(IsValidId(workspace_id));
  return content::StoragePartitionConfig::Create(
      context, content::kAerieWorkspacePartitionDomain, workspace_id, /*in_memory=*/false);
}

bool IsWorkspacePartition(const content::StoragePartitionConfig& config) {
  return config.partition_domain() == content::kAerieWorkspacePartitionDomain &&
         IsValidId(config.partition_name());
}

std::optional<std::string> GetWorkspaceId(
    const content::StoragePartitionConfig& config) {
  if (!IsWorkspacePartition(config)) {
    return std::nullopt;
  }
  return config.partition_name();
}

std::optional<std::string> GetWorkspaceIdForWebContents(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return std::nullopt;
  }
  return GetWorkspaceId(web_contents->GetSiteInstance()
                            ->GetSecurityPrincipal()
                            .GetStoragePartitionConfig());
}

std::optional<content::StoragePartitionConfig> GetWorkspacePartitionToInherit(
    const NavigateParams& params) {
  if (params.storage_partition_config &&
      IsWorkspacePartition(*params.storage_partition_config)) {
    return params.storage_partition_config;
  }
  if (params.source_site_instance) {
    const auto& config = params.source_site_instance->GetSecurityPrincipal()
                             .GetStoragePartitionConfig();
    if (IsWorkspacePartition(config)) {
      return config;
    }
  }
  return std::nullopt;
}

void MoveTabToWorkspace(BrowserWindowInterface* browser,
                        int tab_index,
                        std::string_view target_workspace_id) {
  TabStripModel* strip = browser->GetTabStripModel();
  content::WebContents* old_tab = strip->GetWebContentsAt(tab_index);
  const GURL url = old_tab->GetLastCommittedURL();
  Profile* profile = browser->GetProfile();

  auto site_instance = content::SiteInstance::CreateForFixedStoragePartition(
      profile, url, GetWorkspacePartitionConfig(profile, target_workspace_id));
  auto new_tab = content::WebContents::Create(
      content::WebContents::CreateParams(profile, std::move(site_instance)));
  // No referrer, no opener, no initiator: the page must not learn where it came from.
  new_tab->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(url));

  const bool was_active = strip->active_index() == tab_index;
  strip->InsertWebContentsAt(
      tab_index + 1, std::move(new_tab),
      was_active ? AddTabTypes::ADD_ACTIVE : AddTabTypes::ADD_NONE);
  strip->CloseWebContentsAt(tab_index, TabCloseTypes::CLOSE_NONE);
}

}  // namespace aerie_workspaces
