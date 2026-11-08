// Prototype for issue #72, option 1. Not built.

#include "chrome/browser/aerie_workspaces/workspace_profile_manager.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"

namespace aerie_workspaces {

void GetOrCreateWorkspaceProfile(const std::string& workspace_id,
                                 base::OnceCallback<void(Profile*)> callback) {
  ProfileManager* manager = g_browser_process->profile_manager();
  const base::FilePath path = manager->user_data_dir().AppendASCII(
      "Workspace " + workspace_id);
  if (Profile* existing = manager->GetProfileByPath(path)) {
    std::move(callback).Run(existing);
    return;
  }
  // TODO: LoadProfileByPath for a profile that exists on disk. A new one goes
  // through CreateMultiProfileAsync, which picks its own directory name; the
  // real feature must map workspace id -> directory in a pref, or call
  // CreateAndInitializeProfile(path) (private today, friend access needed).
  ProfileManager::CreateMultiProfileAsync(
      base::UTF8ToUTF16("Workspace " + workspace_id), /*icon_index=*/0,
      /*is_hidden=*/true, std::move(callback));
}

}  // namespace aerie_workspaces
