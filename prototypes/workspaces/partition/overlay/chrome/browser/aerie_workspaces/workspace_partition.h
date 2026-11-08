// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Prototype for issue #72, option 2 (a StoragePartition per Workspace).
// Not built, not in patches/series. See prototypes/workspaces/README.md.

#ifndef CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PARTITION_H_
#define CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PARTITION_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "content/public/browser/aerie_workspace_partition.h"
#include "content/public/browser/storage_partition_config.h"

class BrowserWindowInterface;
struct NavigateParams;
namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace aerie_workspaces {

// The partition name is the Workspace id, a random UUID the browser makes. The
// domain constant is shared with permission_util.cc and
// profile_network_context_service.cc through
// content/public/browser/aerie_workspace_partition.h.
//
// Persistent on disk (in_memory=false), at
// <profile>/Storage/ext/aerieworkspace/<hex of the first 6 bytes of
// SHA-256(id)> (storage_partition_impl_map.cc, GetStoragePartitionPath).
content::StoragePartitionConfig GetWorkspacePartitionConfig(
    content::BrowserContext* context,
    std::string_view workspace_id);

bool IsWorkspacePartition(const content::StoragePartitionConfig& config);

// std::nullopt for the default partition (the "no Workspace" tab).
std::optional<std::string> GetWorkspaceId(
    const content::StoragePartitionConfig& config);
std::optional<std::string> GetWorkspaceIdForWebContents(
    content::WebContents* web_contents);

// Used by browser_navigator.cc: the explicit config in `params`, else the
// config of `params.source_site_instance` if it is a Workspace partition.
std::optional<content::StoragePartitionConfig> GetWorkspacePartitionToInherit(
    const NavigateParams& params);

// Story 95. A fixed-partition BrowsingInstance cannot change partition, so a
// move creates a new WebContents in the target partition, loads the same URL
// and closes the old tab. In-page state (form input, JS state, sessionStorage,
// window.opener) is lost on purpose.
void MoveTabToWorkspace(BrowserWindowInterface* browser,
                        int tab_index,
                        std::string_view target_workspace_id);

}  // namespace aerie_workspaces

#endif  // CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PARTITION_H_
