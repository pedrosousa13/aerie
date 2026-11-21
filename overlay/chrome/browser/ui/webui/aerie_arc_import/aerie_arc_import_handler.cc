// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_arc_import/aerie_arc_import_handler.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_tab_model/arc_import.h"
#include "components/aerie_tab_model/arc_import_apply.h"
#include "components/aerie_tab_model/arc_import_json.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

namespace aerie_arc_import {

namespace {

using aerie_tab_model::ArcParseError;
using aerie_tab_model::ArcParseResult;
using aerie_tab_model::ArcSkipCounts;

// Why a file did not load. kNone: it did.
enum class LoadError {
  kNone,
  kUnreadable,
  kNotAFile,
  kTooLarge,
  kInvalidJson,
  kNotArc,
  kNoSpaces,
};

// The error name the page gets (the "error" of the choose answer).
const char* LoadErrorName(LoadError error) {
  switch (error) {
    case LoadError::kNone:
      return "";
    case LoadError::kUnreadable:
      return "unreadable";
    case LoadError::kNotAFile:
      return "not_a_file";
    case LoadError::kTooLarge:
      return "too_large";
    case LoadError::kInvalidJson:
      return "invalid_json";
    case LoadError::kNotArc:
      return "not_arc";
    case LoadError::kNoSpaces:
      return "no_spaces";
  }
  return "unreadable";
}

LoadError LoadErrorFor(ArcParseError error) {
  switch (error) {
    case ArcParseError::kNoSpaces:
      return LoadError::kNoSpaces;
    case ArcParseError::kNotAnArcSidebar:
      return LoadError::kNotArc;
  }
  return LoadError::kNotArc;
}

// What the thread-pool task found out about the file.
struct LoadOutcome {
  LoadError error = LoadError::kNone;
  ArcParseResult parsed;
};

// Whether `path` names a regular file (after links). Looks at the type before
// anything opens the file: opening a FIFO for reading waits for a writer, and
// a device or a socket is not a file Arc wrote. A file swapped for a FIFO
// between this check and the open is not covered; the page cannot do that.
enum class FileKind { kMissing, kRegular, kOther };
FileKind KindOf(const base::FilePath& path) {
#if BUILDFLAG(IS_POSIX)
  base::stat_wrapper_t info;
  if (base::File::Stat(path, &info) != 0) {
    return FileKind::kMissing;
  }
  return S_ISREG(info.st_mode) ? FileKind::kRegular : FileKind::kOther;
#else
  base::File::Info info;
  if (!base::GetFileInfo(path, &info)) {
    return FileKind::kMissing;
  }
  return info.is_directory ? FileKind::kOther : FileKind::kRegular;
#endif
}

// Runs on a thread-pool sequence, never on the UI thread. The size is known
// before a byte is read, and the read is capped again in case the file grew.
LoadOutcome ReadAndParse(const base::FilePath& path) {
  LoadOutcome outcome;
  switch (KindOf(path)) {
    case FileKind::kMissing:
      outcome.error = LoadError::kUnreadable;
      return outcome;
    case FileKind::kOther:
      outcome.error = LoadError::kNotAFile;
      return outcome;
    case FileKind::kRegular:
      break;
  }
  base::File::Info file_info;
  if (!base::GetFileInfo(path, &file_info)) {
    outcome.error = LoadError::kUnreadable;
    return outcome;
  }
  if (file_info.size < 0 ||
      static_cast<uint64_t>(file_info.size) > aerie_tab_model::kArcMaxFileBytes) {
    outcome.error = LoadError::kTooLarge;
    return outcome;
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents,
                                         aerie_tab_model::kArcMaxFileBytes)) {
    outcome.error = contents.size() >= aerie_tab_model::kArcMaxFileBytes
                        ? LoadError::kTooLarge
                        : LoadError::kUnreadable;
    return outcome;
  }
  std::optional<base::Value> value =
      aerie_tab_model::ParseUntrustedJson(contents);
  contents.clear();
  if (!value) {
    outcome.error = LoadError::kInvalidJson;
    return outcome;
  }
  outcome.parsed = aerie_tab_model::ParseArcSidebar(*value);
  if (outcome.parsed.error) {
    outcome.error = LoadErrorFor(*outcome.parsed.error);
  }
  return outcome;
}

base::ListValue SkippedToList(const ArcSkipCounts& counts) {
  base::ListValue list;
  for (const auto& [reason, count] : counts) {
    if (count <= 0) {
      continue;
    }
    base::DictValue entry;
    entry.Set("reason", aerie_tab_model::ArcSkipReasonToString(reason));
    entry.Set("count", count);
    list.Append(std::move(entry));
  }
  return list;
}

base::DictValue Status(const char* status, const char* error = nullptr) {
  base::DictValue answer;
  answer.Set("status", status);
  if (error) {
    answer.Set("error", error);
  }
  return answer;
}

}  // namespace

struct AerieArcImportHandler::Loaded {
  LoadOutcome outcome;
};

AerieArcImportHandler::AerieArcImportHandler(Profile* profile)
    : profile_(profile) {}

AerieArcImportHandler::~AerieArcImportHandler() {
  DropDialog();
}

void AerieArcImportHandler::DropDialog() {
  if (select_file_dialog_) {
    // The dialog must not call this handler again.
    select_file_dialog_->ListenerDestroyed();
    select_file_dialog_.reset();
  }
}

void AerieArcImportHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aerieArcImport.choose",
      base::BindRepeating(&AerieArcImportHandler::HandleChoose,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieArcImport.import",
      base::BindRepeating(&AerieArcImportHandler::HandleImport,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieArcImport.reset",
      base::BindRepeating(&AerieArcImportHandler::HandleReset,
                          base::Unretained(this)));
}

void AerieArcImportHandler::OnJavascriptDisallowed() {
  weak_factory_.InvalidateWeakPtrs();
  DropDialog();
  choose_callback_id_.reset();
  parsed_.reset();
}

void AerieArcImportHandler::Finish(base::DictValue answer) {
  if (!choose_callback_id_ || !IsJavascriptAllowed()) {
    choose_callback_id_.reset();
    return;
  }
  const base::Value id(*choose_callback_id_);
  choose_callback_id_.reset();
  ResolveJavascriptCallback(id, base::Value(std::move(answer)));
}

void AerieArcImportHandler::HandleChoose(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    // No callback id: there is no promise to reject.
    return;
  }
  const base::Value callback_id(args[0].GetString());
  AllowJavascript();
  if (args.size() != 1) {
    RejectJavascriptCallback(callback_id, base::Value("bad_argument"));
    return;
  }
  if (choose_callback_id_) {
    // The chooser is open, or its file is being read: that press is not over.
    // The second one is turned away, and the first one keeps its dialog.
    RejectJavascriptCallback(callback_id, base::Value("busy"));
    return;
  }
  choose_callback_id_ = args[0].GetString();
  parsed_.reset();

  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          web_ui()->GetWebContents());
  if (!browser) {
    Finish(Status("error", "unreadable"));
    return;
  }
  DropDialog();
  select_file_dialog_ = ui::SelectFileDialog::Create(
      this,
      std::make_unique<ChromeSelectFilePolicy>(web_ui()->GetWebContents()));
  ui::SelectFileDialog::FileTypeInfo file_types;
  file_types.extensions.resize(1);
  file_types.extensions[0].push_back(FILE_PATH_LITERAL("json"));
  file_types.include_all_files = false;
  select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE,
      l10n_util::GetStringUTF16(IDS_SETTINGS_AERIE_ARC_IMPORT_CHOOSER_TITLE),
      base::FilePath(), &file_types, 0, base::FilePath::StringType(),
      browser->GetWindow()->GetNativeWindow(), nullptr);
}

void AerieArcImportHandler::FileSelectionCanceled() {
  DropDialog();
  Finish(Status("cancelled"));
}

void AerieArcImportHandler::FileSelected(const ui::SelectedFileInfo& file,
                                         int index) {
  DropDialog();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          [](base::FilePath path) {
            auto loaded = std::make_unique<Loaded>();
            loaded->outcome = ReadAndParse(path);
            return loaded;
          },
          file.path()),
      base::BindOnce(&AerieArcImportHandler::OnLoaded,
                     weak_factory_.GetWeakPtr()));
}

void AerieArcImportHandler::OnLoaded(std::unique_ptr<Loaded> loaded) {
  if (loaded->outcome.error != LoadError::kNone) {
    Finish(Status("error", LoadErrorName(loaded->outcome.error)));
    return;
  }
  parsed_ = std::move(loaded->outcome.parsed);
  base::ListValue spaces;
  for (size_t i = 0; i < parsed_->spaces.size(); ++i) {
    const aerie_tab_model::ArcSpacePlan& space = parsed_->spaces[i];
    base::DictValue entry;
    entry.Set("index", static_cast<int>(i));
    entry.Set("name", space.name);
    entry.Set("icon", space.icon);
    entry.Set("pinnedTabs", space.PinnedTabCount());
    entry.Set("folders", space.FolderCount());
    entry.Set("favorites", static_cast<int>(space.favorites.size()));
    entry.Set("customNames", space.CustomNameCount());
    spaces.Append(std::move(entry));
  }
  base::DictValue answer = Status("ok");
  answer.Set("spaces", std::move(spaces));
  answer.Set("skipped", SkippedToList(parsed_->file_skipped));
  Finish(std::move(answer));
}

void AerieArcImportHandler::HandleImport(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    // No callback id: there is no promise to reject.
    return;
  }
  const base::Value callback_id(args[0].GetString());
  AllowJavascript();
  if (args.size() != 2 || !args[1].is_int()) {
    RejectJavascriptCallback(callback_id, base::Value("bad_argument"));
    return;
  }
  const int index = args[1].GetInt();
  if (!parsed_ || index < 0 ||
      static_cast<size_t>(index) >= parsed_->spaces.size()) {
    ResolveJavascriptCallback(callback_id,
                              base::Value(Status("error", "no_file")));
    return;
  }
  aerie_tab_model::TabModelService* service =
      aerie_tab_model::TabModelServiceFactory::GetForProfile(profile_);
  if (!service || !service->loaded()) {
    ResolveJavascriptCallback(callback_id,
                              base::Value(Status("error", "not_ready")));
    return;
  }

  aerie_tab_model::ArcApplyOptions options;
  options.untitled_folder_name =
      l10n_util::GetStringUTF8(IDS_SETTINGS_AERIE_ARC_IMPORT_UNTITLED_FOLDER);
  options.fail_at_record_for_testing = fail_at_record_for_testing_;
  std::optional<aerie_tab_model::ArcImportCounts> counts =
      aerie_tab_model::ApplyArcSpacePlan(*service->model(),
                                         parsed_->spaces[index], options);
  if (!counts) {
    ResolveJavascriptCallback(callback_id,
                              base::Value(Status("error", "apply_failed")));
    return;
  }
  // Unloaded tabs: PinnedTabsController makes each with
  // kDeferUntilActivation, so nothing loads and nothing is requested.
  if (aerie_tab_model::PinnedTabsController* pinned = service->pinned_tabs()) {
    pinned->RestoreMissingTabs();
  }
  for (const auto& [reason, count] : parsed_->file_skipped) {
    counts->skipped[reason] += count;
  }
  base::DictValue answer = Status("ok");
  answer.Set("pinnedTabs", counts->pinned_tabs);
  answer.Set("folders", counts->folders);
  answer.Set("favorites", counts->favorites);
  answer.Set("customNames", counts->custom_names);
  answer.Set("skipped", SkippedToList(counts->skipped));
  parsed_.reset();
  ResolveJavascriptCallback(callback_id, base::Value(std::move(answer)));
}

// A message with no promise: nothing to resolve or reject, and its arguments
// are not read.
void AerieArcImportHandler::HandleReset(const base::ListValue&) {
  parsed_.reset();
}

}  // namespace aerie_arc_import
