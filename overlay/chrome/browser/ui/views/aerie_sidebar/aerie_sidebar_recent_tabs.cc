// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_recent_tabs.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_recent_tabs_overlay.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#if defined(USE_AURA)
#include "ui/aura/window.h"
#endif
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "url/gurl.h"

namespace {

ui::Accelerator CtrlTab() {
  return ui::Accelerator(ui::VKEY_TAB, ui::EF_CONTROL_DOWN);
}
ui::Accelerator CtrlShiftTab() {
  return ui::Accelerator(ui::VKEY_TAB, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
}

// The host of the page without a leading "www.", for the line under the title.
// A page without a host (a file, an internal page) shows its scheme.
std::u16string SiteOf(content::WebContents* contents) {
  const GURL& url = contents->GetLastCommittedURL();
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) {
    host.erase(0, 4);
  }
  if (host.empty()) {
    host = std::string(url.scheme());
  }
  return base::UTF8ToUTF16(host);
}

// The page's own icon; for a page that has none (the driver then hands out a
// dull grey default), the globe in the popover text colour, which holds its
// contrast on the overlay's surface in light and dark.
ui::ImageModel FaviconOf(content::WebContents* contents, SkColor icon_color) {
  if (auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents)) {
    gfx::Image image = driver->GetFavicon();
    if (driver->FaviconIsValid() && !image.IsEmpty()) {
      return ui::ImageModel::FromImage(image);
    }
  }
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon, icon_color,
                                        aerie::tokens::kSizeIcon);
}

tabs::TabInterface* TabOf(int id) {
  return tabs::TabHandle(id).Get();
}

}  // namespace

// Sees every key of the window before its target, whether that is the page or
// the address field, and keeps Esc (and its release) from them while the
// overlay is open.
class AerieSidebarRecentTabs::EscapeFilter : public ui::EventHandler {
 public:
  explicit EscapeFilter(AerieSidebarRecentTabs* owner) : owner_(owner) {}

  void OnKeyEvent(ui::KeyEvent* event) override {
    if (event->key_code() != ui::VKEY_ESCAPE || event->handled()) {
      return;
    }
    if (event->type() == ui::EventType::kKeyPressed && owner_->session_.active()) {
      owner_->Cancel();
      swallow_release_ = true;
    } else if (!(event->type() == ui::EventType::kKeyReleased &&
                 swallow_release_)) {
      return;
    }
    if (event->type() == ui::EventType::kKeyReleased) {
      swallow_release_ = false;
    }
    event->SetHandled();
    event->StopPropagation();
  }

 private:
  const raw_ptr<AerieSidebarRecentTabs> owner_;
  bool swallow_release_ = false;
};

AerieSidebarRecentTabs::AerieSidebarRecentTabs(AerieSidebarView* host,
                                               BrowserWindowInterface* browser)
    : host_(host), browser_(browser) {
  TabStripModel* model = browser_->GetTabStripModel();
  // The order starts as the strip order, with the active tab in front: nothing
  // is known about when the other tabs were used.
  for (int i = 0; i < model->count(); ++i) {
    order_.TabAdded(model->GetTabAtIndex(i)->GetHandle().raw_value(), false);
  }
  if (tabs::TabInterface* active = model->GetActiveTab()) {
    order_.TabActivated(active->GetHandle().raw_value());
  }
  model->AddObserver(this);
}

AerieSidebarRecentTabs::~AerieSidebarRecentTabs() {
  if (TabStripModel* model = browser_->GetTabStripModel()) {
    model->RemoveObserver(this);
  }
  // The window is going: no motion, no announcement.
  session_.End();
  monitor_.reset();
  RemoveEscapeFilter();
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerator(CtrlTab(), this);
    focus_manager_->UnregisterAccelerator(CtrlShiftTab(), this);
  }
  DestroyOverlay();
}

void AerieSidebarRecentTabs::Attach() {
  views::Widget* widget = host_->GetWidget();
  if (!widget || focus_manager_) {
    return;
  }
  focus_manager_ = widget->GetFocusManager();
  host_widget_observation_.Observe(widget);
#if defined(USE_AURA)
  escape_filter_ = std::make_unique<EscapeFilter>(this);
  widget->GetNativeWindow()->AddPreTargetHandler(escape_filter_.get());
#endif
  if (focus_manager_) {
    // Before the browser's own Ctrl+Tab and Ctrl+Shift+Tab commands.
    focus_manager_->RegisterAccelerator(
        CtrlTab(), ui::AcceleratorManager::kHighPriority, this);
    focus_manager_->RegisterAccelerator(
        CtrlShiftTab(), ui::AcceleratorManager::kHighPriority, this);
  }
}

void AerieSidebarRecentTabs::RemoveEscapeFilter() {
#if defined(USE_AURA)
  if (escape_filter_) {
    views::Widget* widget = host_->GetWidget();
    if (widget && widget->GetNativeWindow()) {
      widget->GetNativeWindow()->RemovePreTargetHandler(escape_filter_.get());
    }
  }
#endif
  escape_filter_.reset();
}

void AerieSidebarRecentTabs::Detach() {
  Cancel();
  RemoveEscapeFilter();
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerator(CtrlTab(), this);
    focus_manager_->UnregisterAccelerator(CtrlShiftTab(), this);
    focus_manager_ = nullptr;
  }
  host_widget_observation_.Reset();
}

// static
bool AerieSidebarRecentTabs::IsPrivateTab(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  // An incognito window. A Private Workspace (issue #259) is a tab of a
  // private storage partition, and must answer true here as well: when it is
  // built, its check goes on this line, the only gate before a snapshot is
  // taken or shown.
  return !contents || contents->GetBrowserContext()->IsOffTheRecord();
}

bool AerieSidebarRecentTabs::CanHandleAccelerators() const {
  // Read at each press, so that the Settings switch applies at once.
  return host_->profile_prefs() &&
         host_->profile_prefs()->GetBoolean(
             aerie_sidebar::kRecentTabSwitcherPref);
}

bool AerieSidebarRecentTabs::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  const bool backwards = accelerator.IsShiftDown();
  if (session_.active()) {
    Step(backwards);
    return true;
  }
  return Start(backwards);
}

size_t AerieSidebarRecentTabs::MaxCards() const {
  const size_t most = static_cast<size_t>(aerie::tokens::kSizeSwitcherMaxCards);
  views::Widget* widget = host_->GetWidget();
  if (!widget) {
    return most;
  }
  // The overlay keeps room of its own on each side; the cards must fit between.
  const int gap = AerieSidebarRecentTabsOverlay::CardGap();
  const int room = 2 * AerieSidebarRecentTabsOverlay::Inset();
  const int step = aerie::tokens::kSizeSwitcherCardWidth + gap;
  const int fit =
      (widget->GetClientAreaBoundsInScreen().width() - room + gap) / step;
  return std::clamp<size_t>(static_cast<size_t>(std::max(fit, 0)), 2u, most);
}

bool AerieSidebarRecentTabs::Start(bool backwards) {
  TabStripModel* model = browser_->GetTabStripModel();
  std::vector<int> ids;
  for (int id : order_.Order()) {
    tabs::TabInterface* tab = TabOf(id);
    if (tab && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
      ids.push_back(id);
    }
  }
  // Every tab is in the session; the overlay shows MaxCards() of them at a
  // time and slides along the selection.
  if (!session_.Begin(ids, backwards, MaxCards())) {
    return false;
  }
  DestroyOverlay();
  ShowOverlay();
  views::Widget* widget = host_->GetWidget();
  if (widget) {
    monitor_ = views::EventMonitor::CreateWindowMonitor(
        this, widget->GetNativeWindow(),
        {ui::EventType::kKeyPressed, ui::EventType::kKeyReleased});
  }
  SelectionChanged();
  return true;
}

void AerieSidebarRecentTabs::Step(bool backwards) {
  if (backwards) {
    session_.StepBackward();
  } else {
    session_.StepForward();
  }
  SelectionChanged();
}

void AerieSidebarRecentTabs::SelectionChanged() {
  if (!session_.active() || !overlay_) {
    return;
  }
  if (session_.first_visible() != shown_first_) {
    FillCards();
  }
  overlay_->SetSelectedIndex(session_.visible_selected_index());
  AerieRecentTabCard* card = overlay_->card_at(session_.visible_selected_index());
  const std::u16string text = l10n_util::GetStringFUTF16(
      IDS_AERIE_RECENT_TABS_ANNOUNCE,
      l10n_util::GetStringFUTF16(IDS_AERIE_RECENT_TABS_CARD_NAME,
                                 std::u16string(card->title_label()->GetText()),
                                 std::u16string(card->site_label()->GetText())),
      base::NumberToString16(session_.selected_index() + 1),
      base::NumberToString16(session_.cards().size()));
  last_announcement_ = text;
  // The page keeps the focus, so the card is told through the sidebar's view,
  // like the sidebar's other notices.
  host_->GetViewAccessibility().AnnouncePolitely(text);
}

void AerieSidebarRecentTabs::Commit() {
  const int id = session_.selected();
  EndSession();
  CloseOverlay(/*animate=*/true);
  tabs::TabInterface* tab = id >= 0 ? TabOf(id) : nullptr;
  TabStripModel* model = browser_->GetTabStripModel();
  if (tab && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
    model->ActivateTab(
        tab, TabStripUserGestureDetails(
                 TabStripUserGestureDetails::GestureType::kKeyboard));
  }
}

void AerieSidebarRecentTabs::Cancel() {
  if (!session_.active()) {
    return;
  }
  EndSession();
  CloseOverlay(/*animate=*/true);
}

void AerieSidebarRecentTabs::EndSession() {
  session_.End();
  monitor_.reset();
}

void AerieSidebarRecentTabs::OnEvent(const ui::Event& event) {
  if (!session_.active() || !event.IsKeyEvent()) {
    return;
  }
  const ui::KeyEvent& key = *event.AsKeyEvent();
  if (key.type() == ui::EventType::kKeyReleased &&
      (key.key_code() == ui::VKEY_CONTROL ||
       key.key_code() == ui::VKEY_LCONTROL ||
       key.key_code() == ui::VKEY_RCONTROL)) {
    Commit();
  }
#if !defined(USE_AURA)
  else if (key.type() == ui::EventType::kKeyPressed &&
           key.key_code() == ui::VKEY_ESCAPE) {
    Cancel();
  }
#endif
}

void AerieSidebarRecentTabs::OnWidgetActivationChanged(views::Widget* widget,
                                                        bool active) {
  // The window was left with Ctrl held (Alt+Tab to another window): the
  // release will not reach this window, so nothing is chosen.
  if (!active) {
    Cancel();
  }
}

void AerieSidebarRecentTabs::OnWidgetDestroying(views::Widget* widget) {
  Cancel();
  RemoveEscapeFilter();
  DestroyOverlay();
  host_widget_observation_.Reset();
  focus_manager_ = nullptr;
}

void AerieSidebarRecentTabs::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    for (const auto& inserted : change.GetInsert()->contents) {
      order_.TabAdded(inserted.tab->GetHandle().raw_value(), false);
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      const int id = removed.tab->GetHandle().raw_value();
      order_.TabRemoved(id);
      session_.TabRemoved(id);
    }
    if (session_.active()) {
      if (session_.cards().size() < 2) {
        Cancel();
      } else {
        FillCards();
        SelectionChanged();
      }
    }
  }
  if (selection.active_tab_changed() && selection.new_tab) {
    order_.TabActivated(selection.new_tab->GetHandle().raw_value());
  }
}

gfx::ImageSkia AerieSidebarRecentTabs::SnapshotOf(tabs::TabInterface* tab) {
  // The one gate: a private tab is never asked for a snapshot, and a
  // snapshot of one is never shown.
  if (IsPrivateTab(tab) || !snapshot_source_) {
    return gfx::ImageSkia();
  }
  return snapshot_source_.Run(tab);
}

void AerieSidebarRecentTabs::FillCards() {
  const aerie_sidebar::SidebarPalette& palette = host_->palette();
  std::vector<AerieRecentTabCard::Content> contents;
  std::vector<tabs::TabInterface*> shown;
  const std::vector<int> visible = session_.VisibleCards();
  shown_first_ = session_.first_visible();
  for (int id : visible) {
    tabs::TabInterface* tab = TabOf(id);
    AerieRecentTabCard::Content content;
    if (tab && tab->GetContents()) {
      content.title = host_->ShownTitleOfTab(tab);
      content.site = SiteOf(tab->GetContents());
      content.favicon = FaviconOf(tab->GetContents(), palette.popover_text);
      content.snapshot = SnapshotOf(tab);
    }
    if (content.title.empty()) {
      content.title = l10n_util::GetStringUTF16(IDS_AERIE_RECENT_TABS_UNTITLED);
    }
    contents.push_back(std::move(content));
    shown.push_back(tab);
  }
  subscriptions_.clear();
  overlay_->SetCards(contents);
  // Chromium's own snapshot of a tab (the one its hover cards show) is in
  // memory only and arrives decoded. Only tabs that already have one are asked,
  // so that opening this overlay never makes a tab take a picture it did not
  // have, and never for a private tab.
  if (!snapshot_source_) {
    for (size_t i = 0; i < shown.size(); ++i) {
      tabs::TabInterface* tab = shown[i];
      if (!tab || IsPrivateTab(tab)) {
        continue;
      }
      ThumbnailTabHelper* helper = ThumbnailTabHelper::From(tab);
      scoped_refptr<ThumbnailImage> thumbnail =
          helper ? helper->thumbnail() : nullptr;
      if (!thumbnail || !thumbnail->has_data()) {
        continue;
      }
      std::unique_ptr<ThumbnailImage::Subscription> subscription =
          thumbnail->Subscribe();
      subscription->SetSizeHint(
          gfx::Size(aerie::tokens::kSizeSwitcherCardWidth -
                        2 * aerie::tokens::kSpace8,
                    aerie::tokens::kSizeSwitcherSnapshotHeight));
      subscription->SetUncompressedImageCallback(base::BindRepeating(
          [](base::WeakPtr<AerieSidebarRecentTabs> self, int id,
             gfx::ImageSkia image) {
            if (!self || !self->overlay_) {
              return;
            }
            const std::vector<int> cards = self->session_.VisibleCards();
            auto it = std::find(cards.begin(), cards.end(), id);
            if (it != cards.end()) {
              self->overlay_->card_at(static_cast<size_t>(it - cards.begin()))
                  ->SetSnapshot(image);
            }
          },
          weak_factory_.GetWeakPtr(), visible[i]));
      thumbnail->RequestThumbnailImage();
      subscriptions_.push_back(std::move(subscription));
    }
  }
}

void AerieSidebarRecentTabs::ShowOverlay() {
  auto overlay =
      std::make_unique<AerieSidebarRecentTabsOverlay>(host_->palette());
  overlay_ = overlay.get();
  FillCards();

  views::Widget* host = host_->GetWidget();
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_POPUP);
  params.parent = host->GetNativeView();
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.accept_events = false;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
  params.name = "AerieRecentTabs";
  const gfx::Size size = overlay->GetPreferredSize();
  gfx::Rect bounds(size);
  bounds.set_origin(host->GetClientAreaBoundsInScreen().CenterPoint() -
                    gfx::Vector2d(size.width() / 2, size.height() / 2));
  params.bounds = bounds;
  overlay_widget_ = std::make_unique<views::Widget>();
  overlay_widget_->Init(std::move(params));
  overlay_widget_->SetContentsView(std::move(overlay));
  overlay_->SetPaintToLayer();
  overlay_->layer()->SetFillsBoundsOpaquely(false);

  // Motion: the command bar's (fade, and a scale from 0.98 unless reduced).
  const PrefService* prefs = host_->profile_prefs();
  const aerie_sidebar::SwitcherMotion motion = aerie_sidebar::SwitcherOpenMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  ui::Layer* fade_layer = overlay_widget_->GetLayer();
  overlay_widget_->ShowInactive();
  if (fade_layer) {
    fade_layer->SetOpacity(motion.from_opacity);
    aerie::motion::LayerMotion target;
    target.opacity = motion.to_opacity;
    fade_ = aerie::motion::AnimateLayer(fade_layer, target, motion.duration,
                                        motion.easing, prefs);
  }
  if (motion.animate_scale) {
    const gfx::PointF center(size.width() / 2.f, size.height() / 2.f);
    gfx::Transform from;
    from.Translate(center.x(), center.y());
    from.Scale(motion.from_scale, motion.from_scale);
    from.Translate(-center.x(), -center.y());
    overlay_->layer()->SetTransform(from);
    aerie::motion::LayerMotion target;
    target.transform = gfx::Transform();
    scale_ = aerie::motion::AnimateLayer(overlay_->layer(), target,
                                         motion.duration, motion.easing, prefs);
  }
}

void AerieSidebarRecentTabs::CloseOverlay(bool animate) {
  subscriptions_.clear();
  overlay_ = nullptr;
  if (!overlay_widget_) {
    return;
  }
  fade_.Abort();
  scale_.Abort();
  ui::Layer* layer = overlay_widget_->GetLayer();
  if (!animate || !layer) {
    DestroyOverlay();
    return;
  }
  const PrefService* prefs = host_->profile_prefs();
  const aerie_sidebar::SwitcherMotion motion = aerie_sidebar::SwitcherCloseMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  fade_ = aerie::motion::AnimateLayer(layer, target, motion.duration,
                                      motion.easing, prefs);
  close_timer_.Start(FROM_HERE, base::Milliseconds(motion.duration_ms),
                     base::BindOnce(&AerieSidebarRecentTabs::DestroyOverlay,
                                    weak_factory_.GetWeakPtr()));
}

void AerieSidebarRecentTabs::DestroyOverlay() {
  close_timer_.Stop();
  fade_.Abort();
  scale_.Abort();
  subscriptions_.clear();
  overlay_ = nullptr;
  if (overlay_widget_) {
    overlay_widget_->CloseNow();
    overlay_widget_.reset();
  }
}
