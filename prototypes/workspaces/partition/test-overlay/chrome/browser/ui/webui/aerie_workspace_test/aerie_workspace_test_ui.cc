// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_workspace_test/aerie_workspace_test_ui.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/aerie_workspaces/workspace_partition.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "url/gurl.h"

namespace aerie_workspaces {

namespace {

constexpr char kHtml[] = R"(<!doctype html>
<meta charset="utf-8">
<title>Workspace test</title>
<h1>Workspace test (issue #72 prototype)</h1>
<pre id="out"></pre>
<script src="workspace_test.js"></script>
)";

// cr.sendWithPromise without cr.js: the handler answers cr.webUIResponse.
constexpr char kJs[] = R"(
const pending = new Map();
let nextId = 0;
window.cr = {
  webUIResponse(id, ok, value) {
    const p = pending.get(id);
    pending.delete(id);
    if (p) { ok ? p.resolve(value) : p.reject(value); }
  },
  webUIListenerCallback() {},
};
function call(name, ...args) {
  const id = 'ws' + (nextId++);
  return new Promise((resolve, reject) => {
    pending.set(id, {resolve, reject});
    chrome.send('workspaceTest.' + name, [id, ...args]);
  });
}
window.workspaceTest = {
  open: (url, ws, background) => call('open', url, ws || '', !!background),
  list: () => call('list'),
  move: (index, ws) => call('move', index, ws),
  activate: (index) => call('activate', index),
  close: (index) => call('close', index),
};
workspaceTest.list().then(
    l => document.getElementById('out').textContent = JSON.stringify(l, null, 2));
)";

class WorkspaceTestHandler : public content::WebUIMessageHandler {
 public:
  WorkspaceTestHandler() = default;
  ~WorkspaceTestHandler() override = default;

  void RegisterMessages() override {
    Register("workspaceTest.open", &WorkspaceTestHandler::HandleOpen);
    Register("workspaceTest.list", &WorkspaceTestHandler::HandleList);
    Register("workspaceTest.move", &WorkspaceTestHandler::HandleMove);
    Register("workspaceTest.activate", &WorkspaceTestHandler::HandleActivate);
    Register("workspaceTest.close", &WorkspaceTestHandler::HandleClose);
  }

 private:
  using Handler = void (WorkspaceTestHandler::*)(const base::ListValue&);
  void Register(const char* name, Handler handler) {
    web_ui()->RegisterMessageCallback(
        name, base::BindRepeating(handler, base::Unretained(this)));
  }

  BrowserWindowInterface* GetBrowser() {
    tabs::TabInterface* tab =
        tabs::TabInterface::MaybeGetFromContents(web_ui()->GetWebContents());
    return tab ? tab->GetBrowserWindowInterface() : nullptr;
  }

  void Reply(const base::ListValue& args, base::Value value) {
    AllowJavascript();
    ResolveJavascriptCallback(args[0], value);
  }

  void Fail(const base::ListValue& args, const char* why) {
    AllowJavascript();
    RejectJavascriptCallback(args[0], base::Value(why));
  }

  int IndexArg(const base::ListValue& args, size_t i, TabStripModel* strip) {
    if (args.size() <= i || !args[i].is_int()) {
      return -1;
    }
    const int index = args[i].GetInt();
    return (index >= 0 && index < strip->count()) ? index : -1;
  }

  void HandleOpen(const base::ListValue& args) {
    BrowserWindowInterface* browser = GetBrowser();
    if (!browser || args.size() != 4 || !args[1].is_string() ||
        !args[2].is_string() || !args[3].is_bool()) {
      return Fail(args, "bad open");
    }
    NavigateParams params(browser, GURL(args[1].GetString()),
                          ui::PAGE_TRANSITION_TYPED);
    params.disposition = args[3].GetBool()
                             ? WindowOpenDisposition::NEW_BACKGROUND_TAB
                             : WindowOpenDisposition::NEW_FOREGROUND_TAB;
    const std::string& workspace = args[2].GetString();
    if (!workspace.empty()) {
      params.storage_partition_config =
          GetWorkspacePartitionConfig(browser->GetProfile(), workspace);
    }
    Navigate(&params);
    base::DictValue result;
    result.Set("index", browser->GetTabStripModel()->GetIndexOfWebContents(
                            params.navigated_or_inserted_contents));
    Reply(args, base::Value(std::move(result)));
  }

  void HandleList(const base::ListValue& args) {
    BrowserWindowInterface* browser = GetBrowser();
    if (!browser) {
      return Fail(args, "no browser");
    }
    TabStripModel* strip = browser->GetTabStripModel();
    base::ListValue tabs;
    for (int i = 0; i < strip->count(); ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      base::DictValue tab;
      tab.Set("index", i);
      tab.Set("url", contents->GetLastCommittedURL().spec());
      tab.Set("title", contents->GetTitle());
      tab.Set("workspace",
              GetWorkspaceIdForWebContents(contents).value_or(std::string()));
      tab.Set("active", strip->active_index() == i);
      tabs.Append(std::move(tab));
    }
    Reply(args, base::Value(std::move(tabs)));
  }

  void HandleMove(const base::ListValue& args) {
    BrowserWindowInterface* browser = GetBrowser();
    if (!browser) {
      return Fail(args, "no browser");
    }
    const int index = IndexArg(args, 1, browser->GetTabStripModel());
    if (index < 0 || args.size() != 3 || !args[2].is_string() ||
        args[2].GetString().empty()) {
      return Fail(args, "bad move");
    }
    MoveTabToWorkspace(browser, index, args[2].GetString());
    Reply(args, base::Value(base::DictValue()));
  }

  void HandleActivate(const base::ListValue& args) {
    BrowserWindowInterface* browser = GetBrowser();
    if (!browser) {
      return Fail(args, "no browser");
    }
    const int index = IndexArg(args, 1, browser->GetTabStripModel());
    if (index < 0) {
      return Fail(args, "bad activate");
    }
    const double ms = base::Time::Now().InMillisecondsFSinceUnixEpoch();
    browser->GetTabStripModel()->ActivateTabAt(index);
    base::DictValue result;
    result.Set("ms", ms);
    Reply(args, base::Value(std::move(result)));
  }

  void HandleClose(const base::ListValue& args) {
    BrowserWindowInterface* browser = GetBrowser();
    if (!browser) {
      return Fail(args, "no browser");
    }
    const int index = IndexArg(args, 1, browser->GetTabStripModel());
    if (index < 0) {
      return Fail(args, "bad close");
    }
    browser->GetTabStripModel()->CloseWebContentsAt(index,
                                                    TabCloseTypes::CLOSE_NONE);
    Reply(args, base::Value(base::DictValue()));
  }
};

}  // namespace

bool WorkspaceTestUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      kWorkspaceTestSwitch);
}

WorkspaceTestUI::WorkspaceTestUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kWorkspaceTestHost);
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(
          [](const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            const bool js = path == "workspace_test.js";
            std::move(callback).Run(
                base::MakeRefCounted<base::RefCountedString>(
                    std::string(js ? kJs : kHtml)));
          }));
  web_ui->AddMessageHandler(std::make_unique<WorkspaceTestHandler>());
}

WorkspaceTestUI::~WorkspaceTestUI() = default;

}  // namespace aerie_workspaces
