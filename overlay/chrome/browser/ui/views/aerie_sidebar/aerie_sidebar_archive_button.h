// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_BUTTON_H_

#include "base/functional/callback.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/label_button.h"

class PrefService;

// The Archive button in the bottom bar of the sidebar (issue #183). It opens
// the archive panel, and it is where an archived tab's copy lands: when it
// lands the button pulses once (docs/sidebar-design.md, "Motion", row "Archive
// moment").
//
// The pulse is the transform of the button's layer: it scales to
// `motion-scale-archive-pulse` and back over `archive-pulse` on `standard`, half
// of the time each way. Under reduced motion nothing scales: the button flashes
// its colour for 140 ms (a static state, not an animation). The layer exists
// only while the pulse runs.
class AerieSidebarArchiveButton : public views::LabelButton {
  METADATA_HEADER(AerieSidebarArchiveButton, views::LabelButton)

 public:
  explicit AerieSidebarArchiveButton(PressedCallback callback);
  AerieSidebarArchiveButton(const AerieSidebarArchiveButton&) = delete;
  AerieSidebarArchiveButton& operator=(const AerieSidebarArchiveButton&) =
      delete;
  ~AerieSidebarArchiveButton() override;

  void SetPalette(const aerie_sidebar::SidebarPalette& palette);

  // Plays the pulse (or the colour flash under reduced motion). A pulse that
  // runs is replaced from where it is.
  void Pulse(const PrefService* prefs);
  // True while a pulse or a flash runs, for tests.
  bool pulsing() const { return pulsing_; }
  bool flashing() const { return flashing_; }
  // The layer animation of the pulse, for tests; null when none runs.
  const aerie::motion::MotionHandle& pulse_motion() const { return motion_; }

  // views::LabelButton:
  void OnPaintBackground(gfx::Canvas* canvas) override;

 private:
  void StartFallBack(const PrefService* prefs);
  void EndPulse();

  aerie_sidebar::SidebarPalette palette_;
  bool pulsing_ = false;
  bool flashing_ = false;
  aerie::motion::MotionHandle motion_;
  base::OneShotTimer timer_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_BUTTON_H_
