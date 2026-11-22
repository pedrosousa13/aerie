// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_hover_card.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/events/event.h"
#include "ui/views/focus/focus_manager.h"
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

}  // namespace

// Calls back when the page's title or address changes.
class AerieSidebarHoverCard::TabWatcher : public content::WebContentsObserver {
 public:
  TabWatcher(AerieSidebarHoverCard* owner, content::WebContents* contents)
      : content::WebContentsObserver(contents), owner_(owner) {}

  void TitleWasSet(content::NavigationEntry*) override { owner_->RefreshText(); }
  void PrimaryPageChanged(content::Page&) override { owner_->RefreshText(); }

 private:
  const raw_ptr<AerieSidebarHoverCard> owner_;
};

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
    case Trigger::kKeyPressed:
      // Until the end of this event a focus change is the keyboard's. A key
      // also ends the pointer's wait, which is not the user's attention now.
      key_in_flight_ = true;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(
                         [](base::WeakPtr<AerieSidebarHoverCard> self) {
                           if (self) {
                             self->key_in_flight_ = false;
                           }
                         },
                         weak_factory_.GetWeakPtr()));
      return;
    case Trigger::kDismissed:
      if (anchor == anchor_) {
        Hide();
      }
      return;
    case Trigger::kFocused:
      // Only the keyboard's focus shows a card: not a program's (focus that is
      // restored, or set by code), which the user did not ask for.
      if (!tab || !KeyboardDriven(anchor)) {
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

bool AerieSidebarHoverCard::KeyboardDriven(views::View* anchor) const {
  if (key_in_flight_) {
    return true;
  }
  // Tab and Shift+Tab move focus through the focus manager.
  const views::FocusManager* manager = anchor->GetFocusManager();
  return manager && manager->focus_change_reason() ==
                        views::FocusManager::FocusChangeReason::kFocusTraversal;
}

bool AerieSidebarHoverCard::ShowsFor(const views::View* anchor) const {
  return widget_ && anchor_ == anchor;
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
  tabs::TabInterface* tab = aerie_sidebar::TabOf(tab_id_);
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
    RetireWidget();
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
  tab_watcher_ = std::make_unique<TabWatcher>(this, contents);
  // A press anywhere in the window closes the card, before the press is
  // handled by the row or by a child of it.
  views::Widget* window = anchor_->GetWidget();
  press_monitor_ = views::EventMonitor::CreateWindowMonitor(
      this, window->GetNativeWindow(), {ui::EventType::kMousePressed});

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
  // The page side is read from where the row is in the window, not from the
  // side setting, so that a mirrored UI places the card on the right side.
  const aerie_sidebar::HoverCardBox row = ToBox(anchor_->GetBoundsInScreen());
  const bool right = aerie_sidebar::HoverCardPrefersRight(
      row, ToBox(anchor_->GetWidget()->GetWindowBoundsInScreen()));
  const aerie_sidebar::HoverCardBox placed = aerie_sidebar::PlaceHoverCard(
      row,
      aerie_sidebar::HoverCardBox{0, 0, card_size.width(), card_size.height()},
      ToBox(ScreenBounds()), right, kRowGap);
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
  // The gate is aerie_sidebar::GatedSnapshot's, shared with the recent tabs
  // overlay.
  return aerie_sidebar::GatedSnapshot(tab, snapshot_source_);
}

void AerieSidebarHoverCard::WatchSnapshot(tabs::TabInterface* tab) {
  subscription_.reset();
  // The Settings switch: off keeps the text and shows no picture.
  const PrefService* prefs = host_->profile_prefs();
  if (!prefs || !prefs->GetBoolean(aerie_sidebar::kHoverCardSnapshotsPref)) {
    return;
  }
  if (snapshot_source_) {
    card_->body()->SetSnapshot(SnapshotOf(tab));
    Place();
    return;
  }
  subscription_ = aerie_sidebar::WatchTabSnapshot(
      tab,
      aerie_sidebar::SnapshotSizeHint(
          aerie::tokens::kSizeHoverCardWidth, aerie::tokens::kSpace12,
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
  tab_watcher_.reset();
  press_monitor_.reset();
  host_widget_observation_.Reset();
  if (anchor_ && !description_.empty()) {
    anchor_->GetViewAccessibility().SetDescription(std::u16string());
    description_.clear();
  }
  if (widget_) {
    RetireWidget();
  }
}

void AerieSidebarHoverCard::RetireWidget() {
  card_ = nullptr;
  widget_->Hide();
  // The window is destroyed by a task of its own: closing it inside the key or
  // mouse event that asked for it upsets the native event handling (under X11
  // the test key senders wait for an event that never comes).
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::Widget> widget) {
                       widget->CloseNow();
                     },
                     std::move(widget_)));
}

void AerieSidebarHoverCard::OnEvent(const ui::Event& event) {
  if (event.type() == ui::EventType::kMousePressed) {
    Hide();
  }
}

void AerieSidebarHoverCard::RefreshText() {
  tabs::TabInterface* tab = aerie_sidebar::TabOf(tab_id_);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!widget_ || !anchor_ || !contents) {
    return;
  }
  const aerie_sidebar::SidebarPalette palette =
      aerie_sidebar::PopoverPaletteOf(host_->palette());
  const aerie_sidebar::HoverCardText text = aerie_sidebar::FormatHoverCardText(
      host_->ShownTitleOfTab(tab), contents->GetLastCommittedURL().spec());
  card_->body()->SetText(text,
                         aerie_sidebar::TabCardFavicon(contents, palette.text));
  description_ = aerie_sidebar::HoverCardDescription(text);
  anchor_->GetViewAccessibility().SetDescription(description_);
  Place();
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
