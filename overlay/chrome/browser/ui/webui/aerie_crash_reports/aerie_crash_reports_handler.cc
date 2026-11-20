// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_crash_reports/aerie_crash_reports_handler.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/web_ui.h"

namespace aerie_crash_reports {

namespace {

constexpr base::TaskTraits kFileTraits = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN};

struct Answered {
  bool ok = false;
  std::vector<ReportSummary> reports;
};

// Only fixed fields reach the page: an id, a time, and two plain tokens.
base::DictValue ToValue(const ReportSummary& report) {
  return base::DictValue()
      .Set("id", report.id)
      .Set("time", report.time.InMillisecondsFSinceUnixEpoch())
      .Set("processType", report.process_type)
      .Set("version", report.version);
}

}  // namespace

// static
std::unique_ptr<AerieCrashReportsHandler> AerieCrashReportsHandler::Create(
    Profile* profile) {
  base::FilePath dir;
  base::PathService::Get(chrome::DIR_CRASH_DUMPS, &dir);
  return std::make_unique<AerieCrashReportsHandler>(
      dir, base::BindRepeating(
               [](Profile* profile, const base::FilePath& folder) {
                 platform_util::OpenItem(profile, folder,
                                         platform_util::OPEN_FOLDER,
                                         base::DoNothing());
                 return true;
               },
               base::Unretained(profile)));
}

AerieCrashReportsHandler::AerieCrashReportsHandler(
    const base::FilePath& database_dir,
    OpenFolderCallback open_folder)
    : database_dir_(database_dir), open_folder_(std::move(open_folder)) {}

AerieCrashReportsHandler::~AerieCrashReportsHandler() = default;

void AerieCrashReportsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aerieCrashReports.list",
      base::BindRepeating(&AerieCrashReportsHandler::HandleList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieCrashReports.delete",
      base::BindRepeating(&AerieCrashReportsHandler::HandleDelete,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieCrashReports.deleteAll",
      base::BindRepeating(&AerieCrashReportsHandler::HandleDeleteAll,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieCrashReports.openFolder",
      base::BindRepeating(&AerieCrashReportsHandler::HandleOpenFolder,
                          base::Unretained(this)));
}

void AerieCrashReportsHandler::OnJavascriptDisallowed() {
  weak_factory_.InvalidateWeakPtrs();
}

void AerieCrashReportsHandler::Answer(const base::Value& callback_id,
                                      Work work) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kFileTraits,
      base::BindOnce(
          [](base::FilePath dir, Work work) {
            CrashReportStore store(dir);
            Answered answered;
            answered.ok = std::move(work).Run(store);
            answered.reports = store.List();
            return answered;
          },
          database_dir_, std::move(work)),
      base::BindOnce(
          [](base::WeakPtr<AerieCrashReportsHandler> self,
             base::Value callback_id, Answered answered) {
            if (self) {
              self->Resolve(std::move(callback_id), answered.ok,
                            std::move(answered.reports));
            }
          },
          weak_factory_.GetWeakPtr(), callback_id.Clone()));
}

void AerieCrashReportsHandler::Resolve(
    base::Value callback_id,
    bool ok,
    std::vector<ReportSummary> reports) {
  base::ListValue list;
  for (const ReportSummary& report : reports) {
    list.Append(ToValue(report));
  }
  ResolveJavascriptCallback(
      callback_id, base::Value(base::DictValue()
                                   .Set("ok", ok)
                                   .Set("limit", static_cast<int>(kMaxReports))
                                   .Set("reports", std::move(list))));
}

void AerieCrashReportsHandler::HandleList(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  if (args.size() != 1) {
    RejectJavascriptCallback(args[0], base::Value("bad_argument"));
    return;
  }
  Answer(args[0], base::BindOnce([](CrashReportStore& store) {
           store.Prune();
           return true;
         }));
}

void AerieCrashReportsHandler::HandleDelete(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  if (args.size() != 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("bad_argument"));
    return;
  }
  Answer(args[0], base::BindOnce(
                      [](std::string id, CrashReportStore& store) {
                        return store.Delete(id);
                      },
                      args[1].GetString()));
}

void AerieCrashReportsHandler::HandleDeleteAll(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  if (args.size() != 1) {
    RejectJavascriptCallback(args[0], base::Value("bad_argument"));
    return;
  }
  Answer(args[0], base::BindOnce([](CrashReportStore& store) {
           store.DeleteAll();
           return true;
         }));
}

void AerieCrashReportsHandler::HandleOpenFolder(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  if (args.size() != 1) {
    RejectJavascriptCallback(args[0], base::Value("bad_argument"));
    return;
  }
  // The folder exists once the crash handler has started; make sure of it before the file
  // manager is asked, so that a profile with no crash yet opens an empty folder.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kFileTraits,
      base::BindOnce(
          [](base::FilePath dir) {
            if (!base::CreateDirectory(dir)) {
              return false;
            }
#if BUILDFLAG(IS_POSIX)
            base::SetPosixFilePermissions(dir, 0700);
#endif
            return true;
          },
          database_dir_),
      base::BindOnce(
          [](base::WeakPtr<AerieCrashReportsHandler> self,
             base::Value callback_id, bool exists) {
            if (!self) {
              return;
            }
            const bool opened =
                exists && self->open_folder_.Run(self->database_dir_);
            self->Answer(callback_id,
                         base::BindOnce(
                             [](bool opened, CrashReportStore&) {
                               return opened;
                             },
                             opened));
          },
          weak_factory_.GetWeakPtr(), args[0].Clone()));
}

}  // namespace aerie_crash_reports
