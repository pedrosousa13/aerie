// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_CRASH_REPORTS_AERIE_CRASH_REPORTS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_CRASH_REPORTS_AERIE_CRASH_REPORTS_HANDLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/aerie_crash_reports/crash_report_store.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace aerie_crash_reports {

class AerieCrashReportsBrowserTest;

// The "Crash reports" page of Settings (issue #262, docs/crash-reports.md). A plain message
// handler on chrome://settings only (SettingsUI, patches/crash/0003). Every message is a
// promise and answers the current list:
//
//   aerieCrashReports.list
//   aerieCrashReports.delete(id)    one report, named by the id the list gave
//   aerieCrashReports.deleteAll
//   aerieCrashReports.openFolder    shows the folder in the system file manager
//
//   answer: {ok: bool, limit: int, reports: [{id, time, processType, version}]}
//
// `time` is milliseconds since the epoch. `ok` is false when a delete or the open failed
// (the list is still current). `processType` and `version` are plain tokens, or empty.
//
// The answer never holds the contents of a dump and never a path: a dump may hold page
// memory, and the page names a report by its id only. A message with a callback id and the
// wrong arguments is rejected with "bad_argument". The file work runs on the thread pool.
class AerieCrashReportsHandler : public content::WebUIMessageHandler {
 public:
  // Shows a folder in the file manager. Returns whether it could.
  using OpenFolderCallback = base::RepeatingCallback<bool(const base::FilePath&)>;

  // The handler of a profile's Settings: the database is the one the crash handler writes
  // (<default user data>/Crash Reports) and the folder opens in the system file manager.
  static std::unique_ptr<AerieCrashReportsHandler> Create(Profile* profile);

  // For tests: a database in another folder, and a stand-in for the file manager.
  AerieCrashReportsHandler(const base::FilePath& database_dir,
                           OpenFolderCallback open_folder);
  ~AerieCrashReportsHandler() override;
  AerieCrashReportsHandler(const AerieCrashReportsHandler&) = delete;
  AerieCrashReportsHandler& operator=(const AerieCrashReportsHandler&) = delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

 private:
  friend class AerieCrashReportsBrowserTest;

  void HandleList(const base::ListValue& args);
  void HandleDelete(const base::ListValue& args);
  void HandleDeleteAll(const base::ListValue& args);
  void HandleOpenFolder(const base::ListValue& args);
  // What a message does to the store, on the thread pool. True when it worked.
  using Work = base::OnceCallback<bool(aerie_crash_reports::CrashReportStore&)>;
  // Runs `work` on the thread pool, then answers with the list as it is then.
  void Answer(const base::Value& callback_id, Work work);
  void Resolve(base::Value callback_id,
               bool ok,
               std::vector<aerie_crash_reports::ReportSummary> reports);

  const base::FilePath database_dir_;
  const OpenFolderCallback open_folder_;
  base::WeakPtrFactory<AerieCrashReportsHandler> weak_factory_{this};
};

}  // namespace aerie_crash_reports

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_CRASH_REPORTS_AERIE_CRASH_REPORTS_HANDLER_H_
