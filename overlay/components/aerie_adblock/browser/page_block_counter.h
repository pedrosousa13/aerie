// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_PAGE_BLOCK_COUNTER_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_PAGE_BLOCK_COUNTER_H_

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace aerie_adblock {

// How many requests the Filter Engine blocked for the page a tab shows now
// (issue #10). The toolbar button shows it. It counts what the engine blocked,
// not what another blocker did, and only for requests that came from a frame
// of this tab; workers and requests with no frame are not counted.
//
// The count starts again at zero when the tab commits another document (a
// reload counts again from zero). Same-document navigations keep it. When a
// page is left with a count above zero, or its tab closes, the count goes to
// the histogram Aerie.Adblock.PageBlockedCount (the Bench reads that).
//
// UI thread only.
class PageBlockCounter
    : public content::WebContentsObserver,
      public content::WebContentsUserData<PageBlockCounter> {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnPageBlockedCountChanged(content::WebContents* web_contents,
                                           int count) = 0;
  };

  ~PageBlockCounter() override;

  // Counts one blocked request for the tab that owns the frame. Does nothing
  // if the frame is gone or has no tab.
  static void RecordBlocked(content::GlobalRenderFrameHostId frame_id);

  // The count for |web_contents|, 0 if it has none yet.
  static int CountFor(content::WebContents* web_contents);

  // Observers of |web_contents|' count. Creates the counter if needed.
  static void AddObserverFor(content::WebContents* web_contents, Observer* observer);
  static void RemoveObserverFor(content::WebContents* web_contents, Observer* observer);

  int count() const { return count_; }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<PageBlockCounter>;
  explicit PageBlockCounter(content::WebContents* web_contents);

  void Increment();
  void RecordAndReset();

  int count_ = 0;
  base::ObserverList<Observer> observers_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_PAGE_BLOCK_COUNTER_H_
