// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Prototype for issue #72, option 2. Test builds only. See
// prototypes/workspaces/README.md.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_WORKSPACE_TEST_AERIE_WORKSPACE_TEST_UI_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_WORKSPACE_TEST_AERIE_WORKSPACE_TEST_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

namespace aerie_workspaces {

inline constexpr char kWorkspaceTestHost[] = "workspace-test";
inline constexpr char kWorkspaceTestSwitch[] = "aerie-test-workspaces";

class WorkspaceTestUI;

// chrome://workspace-test. Exists only when the browser was started with
// --aerie-test-workspaces.
class WorkspaceTestUIConfig
    : public content::DefaultWebUIConfig<WorkspaceTestUI> {
 public:
  WorkspaceTestUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kWorkspaceTestHost) {}
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

// chrome.send("workspaceTest.<name>", [callbackId, ...args]), answered with
// cr.webUIResponse(callbackId, true, result):
//   open [url, workspaceId, background]  -> {index}   ("" = no Workspace)
//   list []                              -> [{index, url, title, workspace, active}]
//   move [index, workspaceId]            -> {}
//   activate [index]                     -> {ms}  (wall clock before ActivateTabAt)
//   close [index]                        -> {}
class WorkspaceTestUI : public content::WebUIController {
 public:
  explicit WorkspaceTestUI(content::WebUI* web_ui);
  ~WorkspaceTestUI() override;
  WorkspaceTestUI(const WorkspaceTestUI&) = delete;
  WorkspaceTestUI& operator=(const WorkspaceTestUI&) = delete;
};

}  // namespace aerie_workspaces

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_WORKSPACE_TEST_AERIE_WORKSPACE_TEST_UI_H_
