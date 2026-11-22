// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tab_snapshot.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_hover_card.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class AerieHoverCardView;
class AerieSidebarView;

namespace tabs {
class TabInterface;
}

// The card that previews a tab when the pointer rests on its row in the sidebar,
// or when its row has keyboard focus (issue #270, docs/sidebar-design.md, "Tab
// preview card, as built (#270)"). It shows the title, the host, the full URL
// on a line of its own, and a snapshot of the page when the tab has one.
//
// When: after `tooltip-delay` (400 ms) of the pointer resting on a row, and not
// if it leaves first. When a card was on screen a moment ago, the next row
// shows its card at once, so that sweeping down the list does not wait at each
// row. A row with keyboard focus shows it at once. A press, leaving the row and
// losing focus close it, with no closing motion.
//
// Where: beside the row, on the page side of the sidebar (the left of the row
// when the sidebar is on the right), level with the row's top, kept on the
// screen and never over the row (aerie_sidebar::PlaceHoverCard).
//
// Screen reader: while the card is up, the row's accessible description is the
// host and the URL, so that a screen reader reads what a sighted user sees. The
// card itself is not a stop. The description goes when the card does.
//
// Snapshots: the thumbnail that Chromium keeps for its own tab hover cards,
// through the same path as the recent tabs overlay (aerie_sidebar_tab_
// snapshot.h): in memory only, never written, never for a private tab. The
// Settings switch "Show page previews on tab cards"
// (aerie.sidebar.hover_card_snapshots) turns the snapshot off and keeps the
// text; it is read each time a card opens.
class AerieSidebarHoverCard : public views::ViewObserver,
                              public views::WidgetObserver,
                              public ui::EventObserver {
 public:
  // For tests: the picture of a tab, in place of Chromium's thumbnail.
  using SnapshotSource = aerie_sidebar::SnapshotSource;

  explicit AerieSidebarHoverCard(AerieSidebarView* host);
  AerieSidebarHoverCard(const AerieSidebarHoverCard&) = delete;
  AerieSidebarHoverCard& operator=(const AerieSidebarHoverCard&) = delete;
  ~AerieSidebarHoverCard() override;

  // What `anchor` (a row or a tile of `tab`) tells the card.
  void OnTrigger(views::View* anchor,
                 tabs::TabInterface* tab,
                 aerie_sidebar::HoverCardTrigger trigger);

  // Closes the card at once, and drops a card that is still waiting for its
  // delay.
  void Hide();
  // Whether the card is up for `anchor` (Esc on that row dismisses it).
  bool ShowsFor(const views::View* anchor) const;

  // The pointer or the focus waits for the delay to pass.
  bool pending() const { return timer_.IsRunning(); }
  bool showing() const { return widget_ != nullptr; }
  // The row the card belongs to; null when nothing is shown.
  views::View* anchor() const;
  // The card, or null when nothing is on screen.
  AerieHoverCardView* card() const { return card_; }
  // The card's own rectangle (without the room of its shadow), in screen
  // coordinates.
  gfx::Rect card_bounds_in_screen() const;
  // How long the pointer rests before a card shows.
  static base::TimeDelta ShowDelay();

  void SetSnapshotSourceForTesting(SnapshotSource source) {
    snapshot_source_ = std::move(source);
  }
  // The screen the card must stay in, in place of the display's work area.
  void SetScreenBoundsForTesting(std::optional<gfx::Rect> bounds) {
    screen_for_testing_ = bounds;
  }

  // views::ViewObserver:
  void OnViewIsDeleting(views::View* observed_view) override;
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;
  // ui::EventObserver: a press anywhere in the window, including on a child of
  // the row (its mute mark, its close button), closes the card before the press
  // is handled.
  void OnEvent(const ui::Event& event) override;

 private:
  void Arm(views::View* anchor, tabs::TabInterface* tab);
  void ShowNow();
  void Place();
  void Open(bool animate);
  void SetAnchor(views::View* anchor);
  void WatchSnapshot(tabs::TabInterface* tab);
  gfx::ImageSkia SnapshotOf(tabs::TabInterface* tab);
  gfx::Rect ScreenBounds() const;
  void Close();
  // The text of the card, and the row's description, from the tab as it is now.
  void RefreshText();
  // Hands the widget to a task that destroys it, so that no window is destroyed
  // in the middle of the input event that closed the card.
  void RetireWidget();
  bool KeyboardDriven(views::View* anchor) const;

  // Watches the tab that the card describes, so that a new title or address
  // reaches the card while it is open.
  class TabWatcher;

  const raw_ptr<AerieSidebarView> host_;
  raw_ptr<views::View> anchor_ = nullptr;
  base::ScopedObservation<views::View, views::ViewObserver> anchor_observation_{
      this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      host_widget_observation_{this};
  // The tab the card describes: a handle, so that a closed tab cannot dangle.
  int tab_id_ = -1;
  base::OneShotTimer timer_;
  std::unique_ptr<TabWatcher> tab_watcher_;
  std::unique_ptr<views::EventMonitor> press_monitor_;
  // True during the handling of a key event that reached a row: a focus that
  // comes then is the keyboard's.
  bool key_in_flight_ = false;
  // When the last card went: a card within `ShowDelay()` of it shows the next
  // row's at once.
  base::TimeTicks last_hidden_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<AerieHoverCardView> card_ = nullptr;
  std::unique_ptr<ThumbnailImage::Subscription> subscription_;
  aerie::motion::MotionHandle fade_;
  aerie::motion::MotionHandle drop_;
  SnapshotSource snapshot_source_;
  std::optional<gfx::Rect> screen_for_testing_;
  std::u16string description_;
  base::WeakPtrFactory<AerieSidebarHoverCard> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_H_
