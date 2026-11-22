// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_recent_tabs.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/event_monitor.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class AerieSidebarRecentTabsOverlay;
class AerieSidebarView;
class BrowserWindowInterface;

namespace tabs {
class TabInterface;
}

// Ctrl+Tab and the recent tabs overlay of one window (issue #269).
//
// With the Settings switch "Ctrl+Tab cycles recent tabs" on (the default),
// Ctrl+Tab and Ctrl+Shift+Tab are taken before Chromium's tab-order commands:
// the first press opens an overlay of cards, one for each of the most recently
// used tabs, with the previous tab selected (Ctrl+Shift+Tab selects the least
// recent one shown). While Ctrl is held, Tab steps along and Ctrl+Shift+Tab
// steps back, wrapping round. Releasing Ctrl switches to the selected tab; Esc
// closes the overlay and stays. Ctrl+PageDown and Ctrl+PageUp keep going along
// the tab order. With the switch off, none of this runs and Chromium's own
// commands see the keys.
//
// Scope: the tabs of this window. That stands in for "the current Workspace"
// until Workspaces exist (M5); then the tabs of the open Workspace are what
// this keeps and shows, and nothing else changes.
//
// The overlay is a popup window over the page that never takes focus, so the
// page keeps its focus and the keys reach this object through the window's
// accelerator and event monitors. Esc also reaches the page (it is not a
// reserved key).
//
// Snapshots: a card shows the page's snapshot when Chromium has one. They come
// from the thumbnail that the tab hover cards use (ThumbnailTabHelper): it
// lives in memory only and this class never writes one. A tab that is private
// (an incognito window, and later a Private Workspace, #259) never gets a
// snapshot: it is neither asked for nor shown. IsPrivateTab() is the one place
// to extend for Private Workspaces.
class AerieSidebarRecentTabs : public TabStripModelObserver,
                               public ui::AcceleratorTarget,
                               public ui::EventObserver,
                               public views::WidgetObserver {
 public:
  // For tests: the picture of a tab, in place of Chromium's thumbnail.
  using SnapshotSource =
      base::RepeatingCallback<gfx::ImageSkia(tabs::TabInterface*)>;

  AerieSidebarRecentTabs(AerieSidebarView* host, BrowserWindowInterface* browser);
  AerieSidebarRecentTabs(const AerieSidebarRecentTabs&) = delete;
  AerieSidebarRecentTabs& operator=(const AerieSidebarRecentTabs&) = delete;
  ~AerieSidebarRecentTabs() override;

  // The sidebar is in a window: take Ctrl+Tab and Ctrl+Shift+Tab. And not.
  void Attach();
  void Detach();

  // True from the first Ctrl+Tab until Ctrl is released or Esc is pressed.
  bool showing() const { return session_.active(); }
  // The cards, or null when nothing is on screen.
  AerieSidebarRecentTabsOverlay* overlay() const { return overlay_; }
  // What a screen reader was last told: the selected card and its place.
  const std::u16string& last_announcement() const { return last_announcement_; }

  void SetSnapshotSourceForTesting(SnapshotSource source) {
    snapshot_source_ = std::move(source);
  }

  // True for a tab that must never have a snapshot taken or shown.
  static bool IsPrivateTab(tabs::TabInterface* tab);

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;
  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;
  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  bool Start(bool backwards);
  void Commit();
  void Cancel();
  void EndSession();
  void Step(bool backwards);
  void ShowOverlay();
  void RefreshCards();
  void FillCards();
  void SelectionChanged();
  void CloseOverlay(bool animate);
  void DestroyOverlay();
  // How many cards fit across the window, at most the token's maximum.
  size_t MaxCards() const;
  gfx::ImageSkia SnapshotOf(tabs::TabInterface* tab);

  const raw_ptr<AerieSidebarView> host_;
  const raw_ptr<BrowserWindowInterface> browser_;
  aerie_sidebar::RecentTabs order_;
  aerie_sidebar::RecentTabSession session_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::unique_ptr<views::EventMonitor> monitor_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      host_widget_observation_{this};
  std::unique_ptr<views::Widget> overlay_widget_;
  raw_ptr<AerieSidebarRecentTabsOverlay> overlay_ = nullptr;
  // The thumbnails that are being decoded for the cards on screen. Dropped when
  // the overlay goes.
  std::vector<std::unique_ptr<ThumbnailImage::Subscription>> subscriptions_;
  aerie::motion::MotionHandle fade_;
  aerie::motion::MotionHandle scale_;
  base::OneShotTimer close_timer_;
  std::u16string last_announcement_;
  SnapshotSource snapshot_source_;
  base::WeakPtrFactory<AerieSidebarRecentTabs> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_H_
