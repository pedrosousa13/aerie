// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_MEDIA_CARD_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_MEDIA_CARD_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"

class PrefService;

namespace ui {
class Layer;
}

// The card at the bottom of the sidebar that shows the tab that plays audio or
// video (issue #263): its title, its site, and Play or Pause, Next track and
// Go to tab. It only draws and reports presses; AerieSidebarMediaController
// decides what plays, what the card says and what a press does.
//
// It shows and hides the way a toast does (docs/sidebar-design.md, "Media
// card, as built (#263)"): it rises `motion-distance-toast-rise` while it
// fades in on `fast` and `decelerate`, and fades out on `fast` and
// `accelerate`. Reduced motion: opacity only, no rise. The collapsed rail has
// no room for it, so the rail hides it without a trace.
class AerieSidebarMediaCard : public views::BoxLayoutView {
  METADATA_HEADER(AerieSidebarMediaCard, views::BoxLayoutView)

 public:
  struct Content {
    // What the page set as the track title, or else the tab's title.
    std::u16string title;
    // The host of the page, such as "example.com".
    std::u16string site;
    // True while the media plays; the first button then says Pause.
    bool playing = false;
    // True when the page handles the "next track" action.
    bool next_enabled = false;
  };

  AerieSidebarMediaCard(base::RepeatingClosure play_pause,
                        base::RepeatingClosure next,
                        base::RepeatingClosure go_to_tab);
  AerieSidebarMediaCard(const AerieSidebarMediaCard&) = delete;
  AerieSidebarMediaCard& operator=(const AerieSidebarMediaCard&) = delete;
  ~AerieSidebarMediaCard() override;

  void SetPalette(const aerie_sidebar::SidebarPalette& palette);
  void SetContent(const Content& content);

  // The card is wanted (a tab plays) or not. Both animate, unless the rail
  // hides the card.
  void Appear(const PrefService* prefs);
  void Dismiss(const PrefService* prefs);
  // The collapsed rail hides the card; leaving the rail brings it back when it
  // is wanted. Neither animates.
  void SetRail(bool rail);
  // True from Appear until the fade of Dismiss has ended; false in the rail.
  bool shown() const { return GetVisible(); }
  // True from Appear to Dismiss, in the rail too.
  bool wanted() const { return wanted_; }

  views::Label* title_label() const { return title_; }
  views::Label* site_label() const { return site_; }
  views::ImageButton* play_pause_button() const { return play_pause_; }
  views::ImageButton* next_button() const { return next_; }
  views::LabelButton* go_to_tab_button() const { return go_to_tab_; }
  // The layer that carries the fade and the rise, null while none is needed.
  ui::Layer* motion_layer() { return layer(); }

 private:
  void StartShow(const PrefService* prefs);
  void FinishHide();
  void UpdateColours();
  void PrepareIconButton(views::ImageButton* button);

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> site_ = nullptr;
  raw_ptr<views::ImageButton> play_pause_ = nullptr;
  raw_ptr<views::ImageButton> next_ = nullptr;
  raw_ptr<views::LabelButton> go_to_tab_ = nullptr;

  aerie_sidebar::SidebarPalette palette_;
  bool playing_ = false;
  bool wanted_ = false;
  bool rail_ = false;
  aerie::motion::MotionHandle motion_;
  base::OneShotTimer hide_timer_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_MEDIA_CARD_H_
