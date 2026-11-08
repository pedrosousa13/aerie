// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Prototype for issue #72, option 1 (a Chromium profile per Workspace).
// Not built, not in patches/series. See prototypes/workspaces/README.md.

#ifndef CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PROFILE_MANAGER_H_
#define CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PROFILE_MANAGER_H_

#include <string>

#include "base/functional/callback.h"

class Profile;

namespace aerie_workspaces {

// Creates (or loads) the hidden profile that backs one Workspace. The profile
// lives in <user-data-dir>/Workspace <id>/ and is hidden from the profile
// picker (is_hidden=true; Aerie has no profile picker in its UI anyway).
// `callback` runs once the profile is initialized, on the UI thread.
void GetOrCreateWorkspaceProfile(const std::string& workspace_id,
                                 base::OnceCallback<void(Profile*)> callback);

}  // namespace aerie_workspaces

#endif  // CHROME_BROWSER_AERIE_WORKSPACES_WORKSPACE_PROFILE_MANAGER_H_
