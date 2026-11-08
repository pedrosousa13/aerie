// Prototype for issue #72, option 2 (a StoragePartition per Workspace).
// Not built, not in patches/series. See prototypes/workspaces/README.md.

#ifndef CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PARTITION_H_
#define CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PARTITION_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "content/public/browser/storage_partition_config.h"

class Browser;
class Profile;
struct NavigateParams;
namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace aerie_workspaces {

// The partition domain is [a-z]* (see StoragePartitionConfig::Create). The
// partition name is the Workspace id, a random UUID the browser makes.
// permission_util.cc and profile_network_context_service.cc compare against
// this literal, because components/ and net/ cannot include chrome/.
inline constexpr char kPartitionDomain[] = "aerieworkspace";

// Persistent on disk (in_memory=false): <profile>/Partitions/aerieworkspace/<hash of id>.
content::StoragePartitionConfig GetWorkspacePartitionConfig(
    content::BrowserContext* context,
    std::string_view workspace_id);

bool IsWorkspacePartition(const content::StoragePartitionConfig& config);

// "" for the default partition (the "no Workspace" tab).
std::string GetWorkspaceId(const content::StoragePartitionConfig& config);
std::string GetWorkspaceIdForWebContents(content::WebContents* web_contents);

// Used by browser_navigator.cc: the explicit config in `params`, else the
// config of `params.source_site_instance` if it is a Workspace partition.
std::optional<content::StoragePartitionConfig> GetWorkspacePartitionToInherit(
    const NavigateParams& params);

// One config per Workspace the user has. Needed by "Clear browsing data" and
// by the Bench, because BrowsingDataRemoverImpl clears only the default
// partition unless the filter names one. Reads the Workspace list pref.
std::vector<content::StoragePartitionConfig> GetAllWorkspacePartitionConfigs(
    Profile* profile);

// Story 95. A fixed-partition BrowsingInstance cannot change partition, so a
// move creates a new WebContents in the target partition, loads the same URL
// and closes the old tab. In-page state (form input, JS state, sessionStorage,
// window.opener) is lost on purpose.
void MoveTabToWorkspace(Browser* browser,
                        int tab_index,
                        std::string_view target_workspace_id);

}  // namespace aerie_workspaces

#endif  // CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PARTITION_H_
