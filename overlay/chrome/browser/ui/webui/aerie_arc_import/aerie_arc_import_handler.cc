// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_arc_import/aerie_arc_import_handler.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "components/aerie_tab_model/arc_import_apply.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace aerie_arc_import {

namespace {

using aerie_tab_model::ArcParseError;
using aerie_tab_model::ArcParseResult;
using aerie_tab_model::ArcSkipCounts;

// What the thread-pool task found out about the file.
struct LoadOutcome {
  // Empty when the file is fine; else the error name of the answer.
  std::string error;
  ArcParseResult parsed;
};

// Runs on a thread-pool sequence, never on the UI thread. The size is known
// before a byte is read, and the read is capped again in case the file grew.
LoadOutcome ReadAndParse(const base::FilePath& path) {
  LoadOutcome outcome;
  base::File::Info file_info;
  if (!base::GetFileInfo(path, &file_info) || file_info.is_directory) {
    outcome.error = "unreadable";
    return outcome;
  }
  if (file_info.size < 0 ||
      static_cast<uint64_t>(file_info.size) > aerie_tab_model::kArcMaxFileBytes) {
    outcome.error = "too_large";
    return outcome;
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents,
                                         aerie_tab_model::kArcMaxFileBytes)) {
    outcome.error = contents.size() >= aerie_tab_model::kArcMaxFileBytes
                        ? "too_large"
                        : "unreadable";
    return outcome;
  }
  std::optional<base::Value> value =
      aerie_tab_model::ParseUntrustedJson(contents);
  contents.clear();
  if (!value) {
    outcome.error = "invalid_json";
    return outcome;
  }
  outcome.parsed = aerie_tab_model::ParseArcSidebar(*value);
  if (outcome.parsed.error) {
    outcome.error = *outcome.parsed.error == ArcParseError::kNoSpaces
                        ? "no_spaces"
                        : "not_arc";
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
  if (select_file_dialog_) {
    select_file_dialog_->ListenerDestroyed();
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
    return;
  }
  AllowJavascript();
  if (choose_callback_id_) {
    // A second press while the chooser is open: the first one is over.
    Finish(Status("cancelled"));
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
  select_file_dialog_.reset();
  Finish(Status("cancelled"));
}

void AerieArcImportHandler::FileSelected(const ui::SelectedFileInfo& file,
                                         int index) {
  select_file_dialog_.reset();
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
  if (!loaded->outcome.error.empty()) {
    Finish(Status("error", loaded->outcome.error.c_str()));
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
  if (args.size() != 2 || !args[0].is_string()) {
    return;
  }
  const base::Value callback_id(args[0].GetString());
  AllowJavascript();
  if (!args[1].is_int()) {
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

void AerieArcImportHandler::HandleReset(const base::ListValue& args) {
  parsed_.reset();
}

}  // namespace aerie_arc_import
