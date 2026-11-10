// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_SETTINGS_AERIE_GUARD_SETTINGS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_SETTINGS_AERIE_GUARD_SETTINGS_HANDLER_H_

#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace aerie_guard {

// The messages of the Aerie Guard section of Settings (issue #103), a thin
// wrapper around aerie_adblock::FilterListManager. It is added to the
// chrome://settings WebUI only (SettingsUI, patches/adblock/0022). The page
// sends sendWithPromise("aerieGuard.<name>", ...args); every message answers
// with the state below, or rejects the promise for an argument that does not
// fit.
//
//   aerieGuard.getState []                    the state
//   aerieGuard.addList [url]                  add a custom list (https only)
//   aerieGuard.removeList [id]                remove a custom list
//   aerieGuard.setListEnabled [id, enabled]   a list on or off (issue #104)
//   aerieGuard.updateList [id]                download one list again now
//   aerieGuard.updateAll []                   download every list again now
//   aerieGuard.setException [host, enabled]   blocking off/on for a site
//   aerieGuard.setAutoUpdate [enabled]
//   aerieGuard.setDefaultLevel [level]        "aggressive" or "standard": the
//                                             blocking level of sites that have
//                                             none of their own (issue #109)
//
// Every argument is checked for its type and, for a string, its length
// (4096 bytes): the page is Aerie's own, but a message is still input. A
// message that does not fit is rejected and changes nothing; the manager checks
// the values again (list URL, list id, host).
//
// Settings always runs in the regular profile, so the handler acts on the
// regular profile's session and never lists or sets a session exception of an
// incognito window. The lists of the manager are process-wide.
//
// The handler holds no user-facing text. An error reaches the page as a code
// ("exceptionError") and the page shows its own string from the grd.
class AerieGuardSettingsHandler
    : public content::WebUIMessageHandler,
      public aerie_adblock::FilterListManager::Observer {
 public:
  explicit AerieGuardSettingsHandler(Profile* profile);
  ~AerieGuardSettingsHandler() override;
  AerieGuardSettingsHandler(const AerieGuardSettingsHandler&) = delete;
  AerieGuardSettingsHandler& operator=(const AerieGuardSettingsHandler&) =
      delete;

  // The state the page shows, as a dictionary. Public for tests.
  // |add_error| is the manager's text for a list it refused, "" for none.
  // |exception_error| is "", "invalid_host", "table_full" or "locked".
  // |list_error| is "", "required" (EasyList cannot be turned off) or
  // "unknown_list".
  //
  // Each list carries "enabled", "required" (true: it cannot be turned off),
  // "ignoredExceptions", "status" ("idle", "updating", "updated" or "failed")
  // and "errorCode" (a fixed code, "" for none; the page owns the words).
  static base::DictValue StateToValue(
      const aerie_adblock::FilterListManager& manager,
      std::string_view add_error,
      std::string_view exception_error,
      std::string_view list_error = std::string_view());

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

  // aerie_adblock::FilterListManager::Observer:
  void OnFilterListsChanged() override;

 private:
  void HandleGetState(const base::ListValue& args);
  void HandleAddList(const base::ListValue& args);
  void HandleRemoveList(const base::ListValue& args);
  void HandleSetListEnabled(const base::ListValue& args);
  void HandleUpdateList(const base::ListValue& args);
  void HandleUpdateAll(const base::ListValue& args);
  void HandleSetException(const base::ListValue& args);
  void HandleSetAutoUpdate(const base::ListValue& args);
  void HandleSetDefaultLevel(const base::ListValue& args);

  // Allows JavaScript and starts watching the manager. Returns the manager,
  // or null (and rejects the callback in |args|) when it does not exist yet.
  aerie_adblock::FilterListManager* Begin(const base::ListValue& args);
  void Resolve(const base::ListValue& args,
               std::string_view add_error,
               std::string_view exception_error,
               std::string_view list_error = std::string_view());
  void Reject(const base::ListValue& args, std::string_view code);

  raw_ptr<Profile> profile_;
  base::ScopedObservation<aerie_adblock::FilterListManager,
                          aerie_adblock::FilterListManager::Observer>
      observation_{this};
};

}  // namespace aerie_guard

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_SETTINGS_AERIE_GUARD_SETTINGS_HANDLER_H_
