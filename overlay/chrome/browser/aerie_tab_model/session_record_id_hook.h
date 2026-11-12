// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_SESSION_RECORD_ID_HOOK_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_SESSION_RECORD_ID_HOOK_H_

#include <map>
#include <string>

namespace content {
class WebContents;
}  // namespace content

namespace aerie_tab_model {

// The two calls that connect Chromium's session file to the Tab Model (issue
// #223). A Today tab's record id is saved with the tab as tab "extra data" under
// this key (the same per-tab key and value map that session restore already
// reads, see sessions::SessionTab::extra_data).
inline constexpr char kSessionRecordIdKey[] = "aerie.tab_record_id";

// Called by chrome::AddRestoredTab (patches/sidebar/0040) with the extra data of
// the tab it creates, before the tab goes into its strip. When the data carries
// a valid record id, the id is kept on `contents` until TabStripAdapter reads
// it when the tab is inserted. A missing or malformed value is ignored.
void StashRestoredRecordId(content::WebContents* contents,
                           const std::map<std::string, std::string>& extra_data);

// Reads what StashRestoredRecordId kept on `contents` and forgets it. An empty
// string when there is none. TabStripAdapter calls this when it sees the tab.
std::string TakeRestoredRecordId(content::WebContents* contents);

// Called by SessionService::BuildCommandsForTab (patches/sidebar/0039) when the
// session is rebuilt. The record id to save for `contents`, or an empty string
// when it has none to save (a tab with no record, or a record that is not in
// Today).
std::string RecordIdToSave(content::WebContents* contents);

// This header has no dependencies, so that //chrome/browser/sessions and
// //chrome/browser/ui can include it without a cycle. The definitions are in
// session_record_id_hook.cc.

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_SESSION_RECORD_ID_HOOK_H_
