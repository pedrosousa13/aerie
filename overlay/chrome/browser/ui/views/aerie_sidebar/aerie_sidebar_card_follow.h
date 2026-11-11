// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CARD_FOLLOW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CARD_FOLLOW_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class PrefService;

// The page card follows the sidebar when it shows or hides (issue #215,
// docs/sidebar-design.md, "Window frame and page card"). The layout gives the
// card its new bounds in one step, as for any width change, and the card then
// follows with a transform from where it was to where it is now. Nothing lays
// out per frame, and only the transform animates.
//
// Arm() before the layout that moves the card. The first change of the card's
// bounds that moves the edge facing the sidebar starts the animation, from the
// distance the edge moved. Reduced motion never arms it: the card snaps.
class AerieSidebarCardFollow : public views::ViewObserver {
 public:
  AerieSidebarCardFollow();
  AerieSidebarCardFollow(const AerieSidebarCardFollow&) = delete;
  AerieSidebarCardFollow& operator=(const AerieSidebarCardFollow&) = delete;
  ~AerieSidebarCardFollow() override;

  // The card, which is the view the window layout sizes (the multi contents
  // view). Call once.
  void Observe(views::View* card);

  // `outer_edge_sign` is -1 for a sidebar on the left, +1 on the right: it
  // tells which edge of the card faces the sidebar.
  void Arm(int outer_edge_sign,
           aerie::tokens::MotionDuration duration,
           aerie::tokens::MotionEasing easing,
           const PrefService* prefs);

  // Stops following: the card keeps where the layout put it.
  void Cancel();

  bool armed() const { return armed_; }
  bool observing() const { return !!card_; }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void Disarm();
  void Start(int layout_delta);
  void ReleaseLayer();
  int EdgeOf(const gfx::Rect& bounds) const;

  raw_ptr<views::View> card_ = nullptr;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
  bool armed_ = false;
  int outer_edge_sign_ = -1;
  aerie::tokens::MotionDuration duration_ = aerie::tokens::MotionDuration::kBase;
  aerie::tokens::MotionEasing easing_ = aerie::tokens::MotionEasing::kStandard;
  raw_ptr<const PrefService> prefs_ = nullptr;
  // The edge of the card facing the sidebar, in the parent's coordinates, as
  // the layout last put it.
  int last_edge_ = 0;
  bool owns_layer_ = false;
  aerie::motion::MotionHandle motion_;
  base::OneShotTimer arm_timer_;
  base::OneShotTimer release_timer_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CARD_FOLLOW_H_
