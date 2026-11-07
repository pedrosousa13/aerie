// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_FILTERS_AERIE_FILTERS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_FILTERS_AERIE_FILTERS_HANDLER_H_

#include <string>

#include "base/scoped_observation.h"
#include "base/values.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace aerie_adblock {

// The messages of chrome://aerie-filters. The page sends
// chrome.send("aerieFilters.<name>", [args]); the handler answers by calling
// aerieFilters.setState(state) in the page, now and after every change.
//
//   getState []                      the page is ready; send the state
//   addList [url]                    add a custom list (https only)
//   removeList [id]                  remove a custom list
//   updateList [id], updateAll []    download again now
//   setException [host, enabled]     blocking off/on for a site
//   setAutoUpdate [enabled]
//
// Every argument is checked: the page is Aerie's own, but a message is still
// input. A message that does not fit is ignored.
class AerieFiltersHandler : public content::WebUIMessageHandler,
                            public FilterListManager::Observer {
 public:
  AerieFiltersHandler();
  ~AerieFiltersHandler() override;
  AerieFiltersHandler(const AerieFiltersHandler&) = delete;
  AerieFiltersHandler& operator=(const AerieFiltersHandler&) = delete;

  // The state the page shows, as a dictionary. Public for tests.
  static base::DictValue StateToValue(const FilterListManager& manager,
                                      const std::string& add_error);

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

  // FilterListManager::Observer:
  void OnFilterListsChanged() override;

 private:
  void HandleGetState(const base::ListValue& args);
  void HandleAddList(const base::ListValue& args);
  void HandleRemoveList(const base::ListValue& args);
  void HandleUpdateList(const base::ListValue& args);
  void HandleUpdateAll(const base::ListValue& args);
  void HandleSetException(const base::ListValue& args);
  void HandleSetAutoUpdate(const base::ListValue& args);
  void PushState();

  bool page_ready_ = false;
  std::string add_error_;
  base::ScopedObservation<FilterListManager, FilterListManager::Observer>
      observation_{this};
};

}  // namespace aerie_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_FILTERS_AERIE_FILTERS_HANDLER_H_
