// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_ARC_IMPORT_AERIE_ARC_IMPORT_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_ARC_IMPORT_AERIE_ARC_IMPORT_HANDLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/aerie_tab_model/arc_import.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/shell_dialogs/select_file_dialog.h"

class Profile;

namespace aerie_arc_import {

// "Import from Arc" in Settings (issue #171, docs/arc-import.md). A plain message
// handler on chrome://settings only (SettingsUI, patches/sidebar/0052). The
// page sends three messages; the first two are promises.
//
//   aerieArcImport.choose
//     Opens the system file chooser on a .json file. The answer is
//       {status: "cancelled"}
//       {status: "error", error: "too_large" | "unreadable" | "not_a_file" |
//                                "invalid_json" | "not_arc" | "no_spaces"}
//       {status: "ok", spaces: [{index, name, icon, pinnedTabs, folders,
//                                favorites, customNames}], skipped: [{reason,
//                                count}]}
//     `name` is empty for a Space Arc left without one, and every string is the
//     file's own text: the page shows it as text and never as markup.
//     The file is refused before it is read when it is over 32 MiB. It is read and
//     parsed on a thread-pool sequence, and nothing is written to the profile.
//
//   A second choose while the chooser is open (or its file is being read) is
//   rejected with "busy"; a message with a callback id and the wrong arguments
//   is rejected with "bad_argument".
//
//   aerieArcImport.import(index)
//     Imports the Space `index` of the last chosen file into the current
//     Workspace (rolled back if a write fails), and makes the unloaded tabs. The answer is
//       {status: "error", error: "no_file" | "not_ready" | "apply_failed"}
//       {status: "ok", pinnedTabs, folders, favorites, customNames,
//        skipped: [{reason, count}]}
//     No page is loaded and no request is made.
//
//   aerieArcImport.reset
//     Forgets the chosen file (the dialog closed).
//
// The page cannot name a path: the only file the handler reads is the one the
// user picks in the chooser.
class AerieArcImportHandler : public content::WebUIMessageHandler,
                              public ui::SelectFileDialog::Listener {
 public:
  explicit AerieArcImportHandler(Profile* profile);
  ~AerieArcImportHandler() override;
  AerieArcImportHandler(const AerieArcImportHandler&) = delete;
  AerieArcImportHandler& operator=(const AerieArcImportHandler&) = delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

  // Test seam: the write of record N of the next import fails.
  void set_fail_at_record_for_testing(int index) {
    fail_at_record_for_testing_ = index;
  }

 private:
  friend class AerieArcImportBrowserTest;

  struct Loaded;

  void HandleChoose(const base::ListValue& args);
  void HandleImport(const base::ListValue& args);
  void HandleReset(const base::ListValue& args);
  void OnLoaded(std::unique_ptr<Loaded> loaded);
  void Finish(base::DictValue answer);
  // Lets go of the file chooser, and tells it first that nobody listens.
  void DropDialog();

  const raw_ptr<Profile> profile_;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  // The callback id of the choose that waits for the user or for the parse.
  std::optional<std::string> choose_callback_id_;
  // What the last good file held. Plain data; nothing in it is a path or a
  // script.
  std::optional<aerie_tab_model::ArcParseResult> parsed_;
  int fail_at_record_for_testing_ = -1;
  base::WeakPtrFactory<AerieArcImportHandler> weak_factory_{this};
};

}  // namespace aerie_arc_import

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_ARC_IMPORT_AERIE_ARC_IMPORT_HANDLER_H_
