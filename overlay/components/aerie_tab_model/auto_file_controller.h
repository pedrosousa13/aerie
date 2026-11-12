// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_AUTO_FILE_CONTROLLER_H_
#define COMPONENTS_AERIE_TAB_MODEL_AUTO_FILE_CONTROLLER_H_

#include <map>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "url/gurl.h"

namespace aerie_tab_model {

class TabModel;

// How long after its creation a tab can still be auto-filed. A tab has no
// address and no title when it is created: they come with the first navigation
// and the page's title, in the first moments. Five seconds covers a slow first
// response and a late title, and is short enough that a page the user opened
// and then used is never moved while they read it (issue #139, "Rules apply to
// new tabs only").
inline constexpr base::TimeDelta kAutoFileWindow = base::Seconds(5);

// Files new tabs into auto-filing folders (issue #139, docs/sidebar-design.md,
// "Folders"). Headless: it observes the Tab Model for new tabs and is told by
// its owner when a tab's page has an address or a title (the owner is the one
// that can see the Chromium tab). It does no work at startup and none for a tab
// that is not new: it only ever holds the tabs created in the last
// kAutoFileWindow.
//
// For each new tab:
//  - the first committed address fixes the page the rules are asked about; a
//    later navigation of the same tab never changes it, and never files the tab;
//  - the title that arrives later is matched too, against that address;
//  - when a folder matches, the tab waits `delay` (`motion-timing-autofile-delay`,
//    so that the move reads as an effect of the new tab), then moves into the
//    folder and observers hear about it (the folder pulses and a toast offers
//    Undo);
//  - a tab the user moves by hand before that is left where it is.
// The window and the delay run on the injected clock and the thread's task
// runner, so tests drive them with a mock-time task environment.
class AutoFileController : public TabModelObserver {
 public:
  struct Filed {
    std::string tab_id;
    std::string folder_id;
    std::string rule_id;
    // The host of the page, for "Don't file this site".
    std::string host;
  };

  class Observer : public base::CheckedObserver {
   public:
    // The tab went into the folder; `filed.tab_id` has `filed_by` set now.
    virtual void OnTabFiled(const Filed& filed) {}
  };

  using BoolProvider = base::RepeatingCallback<bool()>;
  using DelayProvider = base::RepeatingCallback<base::TimeDelta()>;
  // True when a tab that was just added is a new tab that the user (or a page)
  // opened, and not one that session restore brought back.
  using NewTabFilter = base::RepeatingCallback<bool()>;

  // `model` and `clock` must outlive the controller. `rules_enabled` is the
  // global switch ("Folder rules everywhere"). `delay` is how long a matched tab
  // waits before it moves.
  AutoFileController(TabModel* model,
                     const base::TickClock* clock,
                     BoolProvider rules_enabled,
                     DelayProvider delay,
                     base::TimeDelta window = kAutoFileWindow);
  AutoFileController(const AutoFileController&) = delete;
  AutoFileController& operator=(const AutoFileController&) = delete;
  ~AutoFileController() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Tabs added while this returns false are not watched. Optional.
  void SetNewTabFilter(NewTabFilter filter) { new_tab_filter_ = std::move(filter); }

  // True while the tab is a candidate: created within the window, not filed or
  // moved yet. The owner asks this before it works out a URL and a title, so an
  // old tab costs one map lookup.
  bool IsWatching(const std::string& tab_id) const;
  // The tab's page now: its last committed address (empty before the first
  // navigation commits) and its title. Call it when either changes.
  void OnTabPage(const std::string& tab_id,
                 const GURL& url,
                 std::string_view title);
  // Stops watching the tab: it was not opened by the user's click, or the owner
  // knows it was restored.
  void Forget(const std::string& tab_id);

  size_t watched_count() const { return watched_.size(); }

  // TabModelObserver:
  void OnTabAdded(const TabRecord& tab) override;
  void OnTabMoved(const TabRecord& tab, const Placement& from) override;
  void OnTabRemoved(const TabRecord& tab) override;

 private:
  struct Watched {
    base::TimeTicks deadline;
    // The address that the rules are asked about, once the first navigation
    // committed.
    GURL url;
    bool pending = false;
  };

  void PurgeExpired();
  void FileNow(const std::string& tab_id);

  const raw_ptr<TabModel> model_;
  const raw_ptr<const base::TickClock> clock_;
  BoolProvider rules_enabled_;
  DelayProvider delay_;
  const base::TimeDelta window_;
  NewTabFilter new_tab_filter_;
  std::map<std::string, Watched> watched_;
  // The title the last OnTabPage saw, by tab, for the revalidation after the
  // delay.
  std::map<std::string, std::string> titles_;
  // True while this controller moves a tab itself.
  bool filing_ = false;
  base::ObserverList<Observer> observers_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<AutoFileController> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_AUTO_FILE_CONTROLLER_H_
