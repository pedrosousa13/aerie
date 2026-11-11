// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_card_follow.h"

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/transform.h"

namespace {
// The layout runs in the frame after the pref changes. An arm that no layout
// has used by then belongs to something else (a drag resize), so it lapses.
constexpr base::TimeDelta kArmWindow = base::Milliseconds(300);
}  // namespace

AerieSidebarCardFollow::AerieSidebarCardFollow() = default;
AerieSidebarCardFollow::~AerieSidebarCardFollow() {
  motion_.Abort();
}

void AerieSidebarCardFollow::Observe(views::View* card) {
  CHECK(!card_);
  card_ = card;
  observation_.Observe(card);
  last_edge_ = EdgeOf(card->bounds());
}

int AerieSidebarCardFollow::EdgeOf(const gfx::Rect& bounds) const {
  // The edge that faces the sidebar: the left one of a card with the sidebar on
  // its left.
  return outer_edge_sign_ < 0 ? bounds.x() : bounds.right();
}

void AerieSidebarCardFollow::Arm(int outer_edge_sign,
                                 aerie::tokens::MotionDuration duration,
                                 aerie::tokens::MotionEasing easing,
                                 const PrefService* prefs) {
  if (!card_) {
    return;
  }
  outer_edge_sign_ = outer_edge_sign;
  duration_ = duration;
  easing_ = easing;
  prefs_ = prefs;
  last_edge_ = EdgeOf(card_->bounds());
  armed_ = true;
  arm_timer_.Start(FROM_HERE, kArmWindow,
                   base::BindOnce(&AerieSidebarCardFollow::Disarm,
                                  base::Unretained(this)));
}

void AerieSidebarCardFollow::Disarm() {
  armed_ = false;
  arm_timer_.Stop();
}

void AerieSidebarCardFollow::Cancel() {
  Disarm();
  motion_.Abort();
  release_timer_.Stop();
  if (card_ && card_->layer()) {
    card_->layer()->SetTransform(gfx::Transform());
  }
  ReleaseLayer();
}

void AerieSidebarCardFollow::OnViewBoundsChanged(views::View* observed_view) {
  const int edge = EdgeOf(observed_view->bounds());
  const int delta = last_edge_ - edge;
  last_edge_ = edge;
  if (armed_ && delta != 0) {
    Disarm();
    Start(delta);
  }
}

void AerieSidebarCardFollow::OnViewIsDeleting(views::View* observed_view) {
  motion_.Abort();
  observation_.Reset();
  card_ = nullptr;
}

void AerieSidebarCardFollow::Start(int layout_delta) {
  if (!card_) {
    return;
  }
  release_timer_.Stop();
  motion_.Abort();
  if (!card_->layer()) {
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    owns_layer_ = true;
  }
  ui::Layer* layer = card_->layer();
  // The card is drawn where the layout had it, plus what it still had to
  // travel from an earlier follow. It starts there and ends at its new bounds.
  const float visible_now = layer->transform().To2dTranslation().x();
  layer->SetTransform(gfx::Transform::MakeTranslation(
      static_cast<float>(layout_delta) + visible_now, 0));
  aerie::motion::LayerMotion target;
  target.transform = gfx::Transform();
  motion_ = aerie::motion::AnimateLayer(layer, target, duration_, easing_,
                                        prefs_);
  release_timer_.Start(
      FROM_HERE,
      base::Milliseconds(aerie::tokens::GetDurationMs(duration_, false) + 50),
      base::BindOnce(&AerieSidebarCardFollow::ReleaseLayer,
                     base::Unretained(this)));
}

void AerieSidebarCardFollow::ReleaseLayer() {
  if (owns_layer_ && card_ && card_->layer() && !motion_.IsAnimating()) {
    card_->DestroyLayer();
    owns_layer_ = false;
  }
}
