// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_SWITCH_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_SWITCH_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class PrefService;

// A switch (the on and off control of the folder editor sheet, issue #139). It
// says `switch` to a screen reader, takes one Tab stop, and toggles with a
// click, Space or Enter.
//
// views::ToggleButton animates its thumb with a slide animation of its own and
// a fixed 80 ms, which cannot follow the `switch` motion token (120 ms, 60 ms
// reduced) and is not a layer animation. This view has the thumb on a layer, so
// a change moves only `transform` (the thumb) and `opacity` (the accent track),
// through aerie::motion::AnimateLayer. Reduced motion never animates the
// transform: the thumb is at its place at once and the track fades on the
// reduced duration.
class AerieSidebarSwitch : public views::View {
  METADATA_HEADER(AerieSidebarSwitch, views::View)

 public:
  using ChangedCallback = base::RepeatingCallback<void(bool on)>;

  AerieSidebarSwitch(const aerie_sidebar::SidebarPalette& palette,
                     ChangedCallback changed);
  AerieSidebarSwitch(const AerieSidebarSwitch&) = delete;
  AerieSidebarSwitch& operator=(const AerieSidebarSwitch&) = delete;
  ~AerieSidebarSwitch() override;

  void SetPalette(const aerie_sidebar::SidebarPalette& palette);

  // The prefs that say whether motion is reduced; a user toggle animates only
  // when they are set. Not owned; outlives the switch.
  void SetMotionPrefs(const PrefService* prefs) { motion_prefs_ = prefs; }

  bool on() const { return on_; }
  // Sets the state without telling `changed`. `motion_prefs` set: the change
  // animates; null (and the first call) set it at once.
  void SetOn(bool on, const PrefService* motion_prefs);

  // The parts, for tests.
  views::View* thumb() const { return thumb_; }
  views::View* accent_track() const { return accent_track_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnFocus() override;

 private:
  void Toggle();
  void ApplyState(bool animate, const PrefService* prefs);
  void UpdateAccessibility();
  // The offset of the thumb from its off place.
  int ThumbTravel() const;

  aerie_sidebar::SidebarPalette palette_;
  ChangedCallback changed_;
  raw_ptr<const PrefService> motion_prefs_ = nullptr;
  bool on_ = false;
  bool state_applied_ = false;
  bool pressed_ = false;
  raw_ptr<views::View> accent_track_ = nullptr;
  raw_ptr<views::View> thumb_ = nullptr;
  aerie::motion::MotionHandle thumb_motion_;
  aerie::motion::MotionHandle track_motion_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_SWITCH_H_
