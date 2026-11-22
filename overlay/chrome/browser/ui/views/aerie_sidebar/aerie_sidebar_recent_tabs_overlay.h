// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_OVERLAY_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_OVERLAY_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

class AerieSidebarPeekSurface;

// One card of the Ctrl+Tab recent tabs overlay (issue #269): a snapshot of the
// page, or the tab's icon large on a quiet fill when there is none, then the
// favicon, the title and the site. The selected card has the Workspace accent
// ring and the selected-row fill. It only draws: the controller says what is on
// it and which card is selected.
class AerieRecentTabCard : public views::View {
  METADATA_HEADER(AerieRecentTabCard, views::View)

 public:
  struct Content {
    std::u16string title;
    std::u16string site;
    ui::ImageModel favicon;
    // Empty when there is no snapshot. In memory only, never from disk, and
    // never of a private tab (AerieSidebarRecentTabs enforces both).
    gfx::ImageSkia snapshot;
  };

  explicit AerieRecentTabCard(const aerie_sidebar::SidebarPalette& palette);
  AerieRecentTabCard(const AerieRecentTabCard&) = delete;
  AerieRecentTabCard& operator=(const AerieRecentTabCard&) = delete;
  ~AerieRecentTabCard() override;

  void SetContent(const Content& content);
  // The snapshot arrives after the card was made (it is decoded off the main
  // path), or goes. Empty shows the large icon.
  void SetSnapshot(const gfx::ImageSkia& snapshot);
  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  views::Label* title_label() const { return title_; }
  views::Label* site_label() const { return site_; }
  // True while a snapshot is drawn on the card.
  bool snapshot_shown() const { return !snapshot_.isNull(); }
  // True while the favicon is drawn in the footer.
  bool favicon_shown() const { return favicon_->GetVisible(); }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  gfx::Rect SnapshotBounds() const;

  aerie_sidebar::SidebarPalette palette_;
  bool selected_ = false;
  gfx::ImageSkia snapshot_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  // The large icon in the snapshot area while there is no snapshot.
  raw_ptr<views::ImageView> placeholder_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> site_ = nullptr;
};

// The row of cards, on a floating card of its own with the sidebar's peek
// shadow. It is the contents of the overlay's widget. The controller fills it,
// moves the selection and puts the widget on screen. It is a list box for a
// screen reader named "Recent tabs"; each card is an option named by its title
// and site.
class AerieSidebarRecentTabsOverlay : public views::View {
  METADATA_HEADER(AerieSidebarRecentTabsOverlay, views::View)

 public:
  explicit AerieSidebarRecentTabsOverlay(
      const aerie_sidebar::SidebarPalette& palette);
  AerieSidebarRecentTabsOverlay(const AerieSidebarRecentTabsOverlay&) = delete;
  AerieSidebarRecentTabsOverlay& operator=(
      const AerieSidebarRecentTabsOverlay&) = delete;
  ~AerieSidebarRecentTabsOverlay() override;

  void SetCards(const std::vector<AerieRecentTabCard::Content>& cards);
  void SetSelectedIndex(size_t index);

  size_t card_count() const { return cards_.size(); }
  size_t selected_index() const { return selected_; }
  AerieRecentTabCard* card_at(size_t index) const { return cards_[index]; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  aerie_sidebar::SidebarPalette palette_;
  raw_ptr<AerieSidebarPeekSurface> surface_ = nullptr;
  std::vector<raw_ptr<AerieRecentTabCard>> cards_;
  size_t selected_ = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_OVERLAY_H_
