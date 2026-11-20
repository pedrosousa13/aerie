// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_crash_reports/aerie_crash_reports_handler.h"

#include <errno.h>

#include <string>
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

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

namespace aerie_crash_reports {

namespace {

constexpr base::TaskTraits kFileTraits = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN};

struct Answered {
  bool ok = false;
  std::vector<ReportSummary> reports;
};

constexpr size_t kMaxTokenLength = 64;

// What came out of a dump is untrusted text. Only letters, digits, '.', '-' and '_' reach
// the page, at most 64 of them; anything else becomes "", which the page shows as unknown.
std::string SafeToken(const std::string& text) {
  if (text.size() > kMaxTokenLength) {
    return std::string();
  }
  for (char c : text) {
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                       c == '_';
    if (!plain) {
      return std::string();
    }
  }
  return text;
}

// Only fixed fields reach the page: an id, a time, and two plain tokens.
base::DictValue ToValue(const ReportSummary& report) {
  return base::DictValue()
      .Set("id", report.id)
      .Set("time", report.time.InMillisecondsFSinceUnixEpoch())
      .Set("processType", SafeToken(report.process_type))
      .Set("version", SafeToken(report.version));
}

// Makes the folder if it is not there, with mode 0700 from the start and no chmod. A folder
// that is there must be a real folder: a link is refused, so nothing is opened or changed
// through it.
bool EnsureFolder(const base::FilePath& dir) {
#if BUILDFLAG(IS_POSIX)
  struct stat st;
  if (lstat(dir.value().c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  if (!base::CreateDirectory(dir.DirName())) {
    return false;
  }
  return mkdir(dir.value().c_str(), 0700) == 0;
#else
  return base::CreateDirectory(dir);
#endif
}

}  // namespace

// static
std::unique_ptr<AerieCrashReportsHandler> AerieCrashReportsHandler::Create(
    Profile* profile) {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_CRASH_DUMPS, &dir)) {
    // No folder is known: every message answers "not ok" with an empty list, instead of
    // quietly listing nothing as if there were no crashes.
    dir.clear();
  }
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

bool AerieCrashReportsHandler::BeginCall(const base::ListValue& args,
                                         size_t expected_args) {
  if (args.empty() || !args[0].is_string()) {
    return false;
  }
  AllowJavascript();
  if (args.size() != expected_args) {
    RejectJavascriptCallback(args[0], base::Value("bad_argument"));
    return false;
  }
  return true;
}

void AerieCrashReportsHandler::Answer(const base::Value& callback_id,
                                      Work work) {
  if (database_dir_.empty()) {
    Resolve(callback_id.Clone(), /*ok=*/false, {});
    return;
  }
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
  if (!BeginCall(args, 1)) {
    return;
  }
  // Not ok when more than the limit are left: a report could not be deleted.
  Answer(args[0], base::BindOnce([](CrashReportStore& store) {
           store.Prune();
           return store.CountDumps() <= kMaxReports;
         }));
}

void AerieCrashReportsHandler::HandleDelete(const base::ListValue& args) {
  if (!BeginCall(args, 2)) {
    return;
  }
  if (!args[1].is_string()) {
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
  if (!BeginCall(args, 1)) {
    return;
  }
  // Not ok when anything is left: a report could not be deleted.
  Answer(args[0], base::BindOnce([](CrashReportStore& store) {
           store.DeleteAll();
           return store.CountDumps() == 0;
         }));
}

void AerieCrashReportsHandler::HandleOpenFolder(const base::ListValue& args) {
  if (!BeginCall(args, 1)) {
    return;
  }
  if (database_dir_.empty()) {
    Answer(args[0], base::BindOnce([](CrashReportStore&) { return false; }));
    return;
  }
  // The folder exists once the crash handler has started; make sure of it before the file
  // manager is asked, so that a profile with no crash yet opens an empty folder.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kFileTraits, base::BindOnce(&EnsureFolder, database_dir_),
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
