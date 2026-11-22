// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_hover_card.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_hover_card_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tab_snapshot.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/display/display.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace {

// The gap between the row and the card.
constexpr int kRowGap = aerie::tokens::kSpace8;

aerie_sidebar::HoverCardBox ToBox(const gfx::Rect& rect) {
  return aerie_sidebar::HoverCardBox{rect.x(), rect.y(), rect.width(),
                                     rect.height()};
}

tabs::TabInterface* TabOf(int id) {
  return id >= 0 ? tabs::TabHandle(id).Get() : nullptr;
}

}  // namespace

AerieSidebarHoverCard::AerieSidebarHoverCard(AerieSidebarView* host)
    : host_(host) {}

AerieSidebarHoverCard::~AerieSidebarHoverCard() {
  Close();
}

// static
base::TimeDelta AerieSidebarHoverCard::ShowDelay() {
  return base::Milliseconds(aerie::tokens::kMotionTimingTooltipDelay);
}

views::View* AerieSidebarHoverCard::anchor() const {
  return widget_ ? anchor_.get() : nullptr;
}

gfx::Rect AerieSidebarHoverCard::card_bounds_in_screen() const {
  if (!widget_ || !card_) {
    return gfx::Rect();
  }
  gfx::Rect bounds = widget_->GetWindowBoundsInScreen();
  bounds.Inset(AerieHoverCardView::Inset());
  return bounds;
}

void AerieSidebarHoverCard::OnTrigger(views::View* anchor,
                                      tabs::TabInterface* tab,
                                      aerie_sidebar::HoverCardTrigger trigger) {
  using Trigger = aerie_sidebar::HoverCardTrigger;
  switch (trigger) {
    case Trigger::kPointerEntered:
      if (!tab) {
        return;
      }
      if (widget_ && anchor_ == anchor) {
        return;
      }
      // A card on screen, or one that went a moment ago, moves to the next row
      // at once; otherwise the pointer must rest for the delay.
      if (widget_ || (!last_hidden_.is_null() &&
                      base::TimeTicks::Now() - last_hidden_ < ShowDelay())) {
        SetAnchor(anchor);
        tab_id_ = tab->GetHandle().raw_value();
        ShowNow();
      } else {
        Arm(anchor, tab);
      }
      return;
    case Trigger::kFocused:
      if (!tab) {
        return;
      }
      SetAnchor(anchor);
      tab_id_ = tab->GetHandle().raw_value();
      ShowNow();
      return;
    case Trigger::kPointerExited:
    case Trigger::kBlurred:
    case Trigger::kPressed:
      if (anchor == anchor_) {
        Hide();
      }
      return;
  }
}

void AerieSidebarHoverCard::Arm(views::View* anchor, tabs::TabInterface* tab) {
  Hide();
  SetAnchor(anchor);
  tab_id_ = tab->GetHandle().raw_value();
  timer_.Start(FROM_HERE, ShowDelay(),
               base::BindOnce(&AerieSidebarHoverCard::ShowNow,
                              weak_factory_.GetWeakPtr()));
}

void AerieSidebarHoverCard::SetAnchor(views::View* anchor) {
  if (anchor_ == anchor) {
    return;
  }
  // The row that had the card loses its description with it.
  if (anchor_ && !description_.empty()) {
    anchor_->GetViewAccessibility().SetDescription(std::u16string());
    description_.clear();
  }
  anchor_observation_.Reset();
  anchor_ = anchor;
  if (anchor_) {
    anchor_observation_.Observe(anchor_);
  }
}

void AerieSidebarHoverCard::ShowNow() {
  timer_.Stop();
  tabs::TabInterface* tab = TabOf(tab_id_);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!anchor_ || !anchor_->GetWidget() || !anchor_->GetVisible() || !contents) {
    Hide();
    return;
  }
  // A card on screen moves to the new row with no fade.
  const bool animate = !widget_;
  if (widget_) {
    fade_.Abort();
    drop_.Abort();
    subscription_.reset();
    widget_->CloseNow();
    widget_.reset();
    card_ = nullptr;
  }
  const aerie_sidebar::SidebarPalette palette =
      aerie_sidebar::PopoverPaletteOf(host_->palette());
  auto view = std::make_unique<AerieHoverCardView>(host_->palette());
  card_ = view.get();
  const aerie_sidebar::HoverCardText text = aerie_sidebar::FormatHoverCardText(
      host_->ShownTitleOfTab(tab), contents->GetLastCommittedURL().spec());
  card_->body()->SetText(text, aerie_sidebar::TabCardFavicon(contents, palette.text));
  // What a screen reader says: the host and the URL, as the row's description.
  description_ = aerie_sidebar::HoverCardDescription(text);
  anchor_->GetViewAccessibility().SetDescription(description_);

  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_POPUP);
  params.parent = anchor_->GetWidget()->GetNativeView();
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.accept_events = false;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
  params.name = "AerieTabPreviewCard";
  params.bounds = gfx::Rect(view->GetPreferredSize());
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_->SetContentsView(std::move(view));
  host_widget_observation_.Reset();
  host_widget_observation_.Observe(anchor_->GetWidget());
  card_->SetPaintToLayer();
  card_->layer()->SetFillsBoundsOpaquely(false);
  Place();
  WatchSnapshot(tab);
  Open(animate);
}

void AerieSidebarHoverCard::Place() {
  if (!widget_ || !anchor_) {
    return;
  }
  card_->InvalidateLayout();
  const gfx::Size card_size = card_->CardSize();
  const aerie_sidebar::HoverCardBox placed = aerie_sidebar::PlaceHoverCard(
      ToBox(anchor_->GetBoundsInScreen()),
      aerie_sidebar::HoverCardBox{0, 0, card_size.width(), card_size.height()},
      ToBox(ScreenBounds()), SidebarOnTheLeft(), kRowGap);
  gfx::Rect bounds(placed.x, placed.y, placed.width, placed.height);
  bounds.Outset(AerieHoverCardView::Inset());
  widget_->SetBounds(bounds);
}

void AerieSidebarHoverCard::Open(bool animate) {
  const PrefService* prefs = host_->profile_prefs();
  ui::Layer* layer = widget_->GetLayer();
  widget_->ShowInactive();
  if (!animate || !layer) {
    return;
  }
  // The popover motion: a 4 px drop and a fade on `fast` and `decelerate`;
  // opacity only under reduced motion. No closing motion.
  const aerie_sidebar::PopoverMotion motion = aerie_sidebar::PopoverOpenMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  layer->SetOpacity(motion.from_opacity);
  aerie::motion::LayerMotion fade;
  fade.opacity = 1.f;
  fade_ = aerie::motion::AnimateLayer(layer, fade, motion.duration,
                                      motion.easing, prefs);
  if (motion.animate_slide) {
    card_->layer()->SetTransform(
        gfx::Transform::MakeTranslation(0, -motion.drop_px));
    aerie::motion::LayerMotion slide;
    slide.transform = gfx::Transform();
    drop_ = aerie::motion::AnimateLayer(card_->layer(), slide, motion.duration,
                                        motion.easing, prefs);
  }
}

gfx::ImageSkia AerieSidebarHoverCard::SnapshotOf(tabs::TabInterface* tab) {
  // The one gate: a private tab is never asked for a snapshot, and a snapshot
  // of one is never shown.
  if (aerie_sidebar::IsPrivateTab(tab) || !snapshot_source_) {
    return gfx::ImageSkia();
  }
  return snapshot_source_.Run(tab);
}

void AerieSidebarHoverCard::WatchSnapshot(tabs::TabInterface* tab) {
  subscription_.reset();
  // The Settings switch: off keeps the text and shows no picture.
  const PrefService* prefs = host_->profile_prefs();
  if (!prefs || !prefs->GetBoolean(aerie_sidebar::kHoverCardSnapshotsPref) ||
      aerie_sidebar::IsPrivateTab(tab)) {
    return;
  }
  if (snapshot_source_) {
    card_->body()->SetSnapshot(SnapshotOf(tab));
    Place();
    return;
  }
  subscription_ = aerie_sidebar::WatchTabSnapshot(
      tab,
      gfx::Size(aerie::tokens::kSizeHoverCardWidth -
                    2 * aerie::tokens::kSpace12,
                aerie::tokens::kSizeHoverCardSnapshotHeight),
      base::BindRepeating(
          [](base::WeakPtr<AerieSidebarHoverCard> self, gfx::ImageSkia image) {
            if (!self || !self->card_) {
              return;
            }
            self->card_->body()->SetSnapshot(image);
            self->Place();
          },
          weak_factory_.GetWeakPtr()));
}

gfx::Rect AerieSidebarHoverCard::ScreenBounds() const {
  if (screen_for_testing_) {
    return *screen_for_testing_;
  }
  return display::Screen::Get()
      ->GetDisplayNearestView(anchor_->GetWidget()->GetNativeView())
      .work_area();
}

bool AerieSidebarHoverCard::SidebarOnTheLeft() const {
  const PrefService* prefs = host_->profile_prefs();
  return !prefs ||
         prefs->GetString(aerie_sidebar::kSidePref) !=
             aerie_sidebar::kSideRight;
}

void AerieSidebarHoverCard::Hide() {
  timer_.Stop();
  const bool was_showing = widget_ != nullptr;
  Close();
  if (was_showing) {
    last_hidden_ = base::TimeTicks::Now();
  }
  SetAnchor(nullptr);
  tab_id_ = -1;
}

void AerieSidebarHoverCard::Close() {
  fade_.Abort();
  drop_.Abort();
  subscription_.reset();
  card_ = nullptr;
  host_widget_observation_.Reset();
  if (anchor_ && !description_.empty()) {
    anchor_->GetViewAccessibility().SetDescription(std::u16string());
    description_.clear();
  }
  if (widget_) {
    widget_->CloseNow();
    widget_.reset();
  }
}

void AerieSidebarHoverCard::OnViewIsDeleting(views::View* observed_view) {
  // The row went (its tab closed, or the sidebar rebuilt it).
  anchor_observation_.Reset();
  anchor_ = nullptr;
  description_.clear();
  Hide();
}

void AerieSidebarHoverCard::OnViewBoundsChanged(views::View* observed_view) {
  Place();
}

void AerieSidebarHoverCard::OnViewVisibilityChanged(views::View* observed_view,
                                                    views::View* starting_view,
                                                    bool visible) {
  if (!observed_view->IsDrawn()) {
    Hide();
  }
}

void AerieSidebarHoverCard::OnWidgetActivationChanged(views::Widget* widget,
                                                      bool active) {
  // Another window (or a menu) took over: the card has nothing to say.
  if (!active) {
    Hide();
  }
}

void AerieSidebarHoverCard::OnWidgetDestroying(views::Widget* widget) {
  host_widget_observation_.Reset();
  Hide();
}
