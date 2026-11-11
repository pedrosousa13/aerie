// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_top_row.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_drag.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "ui/base/models/image_model.h"
#include "ui/base/base_window.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace {

ui::ImageModel FaviconFor(tabs::TabInterface* tab, SkColor icon_color) {
  content::WebContents* contents = tab->GetContents();
  if (contents) {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(contents)) {
      gfx::Image image = driver->GetFavicon();
      if (!image.IsEmpty()) {
        return ui::ImageModel::FromImage(image);
      }
    }
  }
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon, icon_color,
                                        aerie::tokens::kSizeIcon);
}

}  // namespace

AerieSidebarView::AerieSidebarView(BrowserWindowInterface* browser,
                                   base::RepeatingClosure new_tab)
    : browser_(browser), new_tab_(std::move(new_tab)) {
  if (auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
          browser_->GetProfile())) {
    model_ = service->model();
    model_->AddObserver(this);
  }
  palette_ = ResolvePalette();
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(aerie::tokens::kSpace8, aerie::tokens::kSpace8),
      aerie::tokens::kSpace4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  GetViewAccessibility().SetRole(ax::mojom::Role::kNavigation);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NAME));

  // Top row: window controls, the sidebar toggle, Back, Forward, Reload.
  AerieSidebarTopRow::Callbacks top_row_callbacks;
  top_row_callbacks.toggle_sidebar = base::BindRepeating(
      &AerieSidebarView::ToggleSidebar, base::Unretained(this));
  top_row_callbacks.close_window = base::BindRepeating(
      [](BrowserWindowInterface* browser) { browser->GetWindow()->Close(); },
      base::Unretained(browser_.get()));
  top_row_callbacks.minimize_window = base::BindRepeating(
      [](BrowserWindowInterface* browser) { browser->GetWindow()->Minimize(); },
      base::Unretained(browser_.get()));
  top_row_callbacks.toggle_maximize_window = base::BindRepeating(
      [](BrowserWindowInterface* browser) {
        ui::BaseWindow* window = browser->GetWindow();
        if (window->IsMaximized()) {
          window->Restore();
        } else {
          window->Maximize();
        }
      },
      base::Unretained(browser_.get()));
  top_row_ = AddChildView(std::make_unique<AerieSidebarTopRow>(
      std::move(top_row_callbacks), palette_));

  // Address pill: the real location bar goes in when the toolbar has made it.
  pill_ = AddChildView(std::make_unique<AerieSidebarPill>());
  pill_->SetLayoutManager(std::make_unique<views::FillLayout>());
  pill_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  pill_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ADDRESS));

  // New Tab row.
  new_tab_row_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](AerieSidebarView* self) { self->new_tab_.Run(); },
          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_TAB)));
  new_tab_row_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  new_tab_row_->SetImageLabelSpacing(aerie::tokens::kSpace8);
  new_tab_row_->SetPreferredSize(
      gfx::Size(0, aerie::tokens::kSizeRowHeightComfortable));
  new_tab_row_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_TAB));
  // Chromium's ring takes only a colour role; the sidebar draws its own.
  new_tab_row_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(new_tab_row_);
  AerieSidebarFocusRing::Install(new_tab_row_, aerie::tokens::kRadiusMd);

  // Today.
  today_heading_ = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TODAY),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  today_heading_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  today_heading_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(aerie::tokens::kSpace8, aerie::tokens::kSpace8, 0, 0)));

  scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kDisabled));
  scroll_->SetBackgroundColor(std::nullopt);
  // A bounded scroll view sizes its contents to its width; the box layout
  // gives it the room that is left.
  scroll_->ClipHeightTo(0, 1 << 20);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto list = std::make_unique<AerieSidebarTodayTree>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  // The focus ring of a row is size-focus-ring-offset plus its width outside
  // the row, so the rows keep that much room from each other and from the
  // edges of the scrolling list, which would clip the ring.
  constexpr int kRingRoom = static_cast<int>(
      aerie::tokens::kSizeFocusRingOffset + aerie::tokens::kSizeFocusRingWidth);
  list->SetBetweenChildSpacing(kRingRoom);
  list->SetInsideBorderInsets(gfx::Insets(kRingRoom));
  list->SetFocusHandler(base::BindRepeating(&AerieSidebarView::FocusTabStop,
                                            base::Unretained(this)));
  list->GetViewAccessibility().SetRole(ax::mojom::Role::kTree);
  list->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TODAY));
  list_ = scroll_->SetContents(std::move(list));
  layout->SetFlexForView(scroll_, 1);

  // Bottom bar: a placeholder until the Archive button and the Workspace
  // switcher come. The toolbar's remaining buttons (extensions, profile, menu)
  // sit in it so that they stay reachable.
  bottom_bar_ = AddChildView(std::make_unique<views::View>());
  bottom_bar_->SetLayoutManager(std::make_unique<views::FillLayout>());

  browser_->GetTabStripModel()->AddObserver(this);

  if (model_) {
    today_list_ = std::make_unique<aerie_sidebar::TodayList>(model_, this);
  }
  ApplyPalette();
  SyncRows();
  ScheduleSync();
}

AerieSidebarView::~AerieSidebarView() {
  if (model_) {
    model_->RemoveObserver(this);
  }
  // The toolbar outlives nothing here, but its members point at the views that
  // moved out of it. Give them back so that none points at a view that is
  // gone.
  if (adopted_toolbar_) {
    for (views::View* part : adopted_parts_) {
      if (part && part->parent() && part->parent() != adopted_toolbar_) {
        adopted_toolbar_->AddChildView(part->parent()->RemoveChildViewT(part));
      }
    }
  }
}

void AerieSidebarView::AdoptToolbar(const AerieSidebarToolbarParts& parts) {
  CHECK(!adopted_toolbar_);
  CHECK(parts.toolbar);
  adopted_toolbar_ = parts.toolbar;
  for (views::View* part :
       {parts.back.get(), parts.forward.get(), parts.reload.get()}) {
    if (part && part->parent()) {
      adopted_parts_.push_back(part);
      top_row_->AdoptNavigationButton(part);
    }
  }
  if (parts.location_bar && parts.location_bar->parent()) {
    adopted_parts_.push_back(parts.location_bar);
    pill_->AddChildView(
        parts.location_bar->parent()->RemoveChildViewT(parts.location_bar));
  }
  // What is left of the toolbar goes in the bottom bar.
  if (views::View* old_parent = parts.toolbar->parent()) {
    bottom_bar_->AddChildView(old_parent->RemoveChildViewT(parts.toolbar));
  }
  // The toolbar paints its own fill; here the frame tint shows through.
  parts.toolbar->SetBackground(nullptr);
  InvalidateLayout();
}

void AerieSidebarView::SetWindowControls(int leading_inset,
                                         bool draw_window_controls) {
  // The sidebar's own inset already gives some of the room.
  top_row_->SetLeadingInset(
      std::max(0, leading_inset - static_cast<int>(aerie::tokens::kSpace8)));
  top_row_->ShowWindowControls(draw_window_controls);
}

bool AerieSidebarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  gfx::Point in_row = point;
  ConvertPointToTarget(this, top_row_, &in_row);
  return top_row_->GetVisible() && top_row_->IsPositionInWindowCaption(in_row);
}

aerie_sidebar::SidebarPalette AerieSidebarView::ResolvePalette() const {
  const bool dark = GetWidget() && GetWidget()->GetColorMode() ==
                                       ui::ColorProviderKey::ColorMode::kDark;
  return aerie_sidebar::ResolveSidebarPalette(
      aerie_frame::kDefaultTheme,
      dark ? aerie::tokens::ColorMode::kDark : aerie::tokens::ColorMode::kLight,
      model_ ? model_->workspace().colour
             : std::string(aerie_frame::kFallbackColour));
}

void AerieSidebarView::ApplyPalette() {
  palette_ = ResolvePalette();
  top_row_->SetPalette(palette_);
  today_heading_->SetEnabledColor(palette_.text_muted);
  new_tab_row_->SetEnabledTextColors(palette_.text);
  new_tab_row_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kAddIcon, palette_.text_muted,
                                     aerie::tokens::kSizeIcon));
  AerieSidebarFocusRing::Get(new_tab_row_)->SetColor(palette_.accent);
  for (const Row& row : rows_) {
    row.view->SetPalette(palette_);
  }
  for (AerieSidebarRowView* row : closing_rows_) {
    row->SetPalette(palette_);
  }
  // The favicon of a tab without one is drawn in the muted text colour.
  UpdateRows();
}

void AerieSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // A change of the window's colour mode arrives here.
  ApplyPalette();
}

void AerieSidebarView::OnWorkspaceChanged(const aerie_tab_model::Workspace&) {
  ApplyPalette();
}

void AerieSidebarView::OnModelLoaded() {
  ApplyPalette();
}

void AerieSidebarView::ToggleSidebar() {
  aerie_sidebar::ToggleVisible(browser_->GetProfile()->GetPrefs());
}

tabs::TabInterface* AerieSidebarView::FindTab(const std::string& tab_id) const {
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser_->GetProfile());
  if (!service || !service->adapter()) {
    return nullptr;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (service->adapter()->RecordIdForTab(tab) == tab_id) {
      return tab;
    }
  }
  return nullptr;
}

void AerieSidebarView::SyncRows() {
  // The rows' slots change: a drag in progress would move rows that are not
  // where it measured them.
  if (drag_.active) {
    CancelDrag();
  }
  std::vector<std::string> wanted;
  if (today_list_) {
    for (const std::string& id : today_list_->rows()) {
      if (FindTab(id)) {
        wanted.push_back(id);
      }
    }
  }
  const bool animate = CanAnimateRows();
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();

  // Keep the views of rows that stay, drop the others, add the new ones, and
  // put them in order.
  std::map<std::string, raw_ptr<AerieSidebarRowView>> existing;
  std::map<std::string, int> old_index;
  for (size_t i = 0; i < rows_.size(); ++i) {
    existing[rows_[i].tab_id] = rows_[i].view;
    old_index[rows_[i].tab_id] = static_cast<int>(i);
  }
  const int old_count = static_cast<int>(rows_.size());
  std::vector<Row> next;
  std::vector<AerieSidebarRowView*> added;
  for (const std::string& id : wanted) {
    auto it = existing.find(id);
    if (it != existing.end()) {
      next.push_back({id, it->second});
      existing.erase(it);
      continue;
    }
    AerieSidebarRowView::Callbacks callbacks;
    callbacks.select = base::BindRepeating(&AerieSidebarView::SelectTab,
                                           base::Unretained(this), id);
    callbacks.close = base::BindRepeating(&AerieSidebarView::CloseTab,
                                          base::Unretained(this), id);
    callbacks.key = base::BindRepeating(&AerieSidebarView::OnRowKey,
                                        base::Unretained(this), id);
    callbacks.focused = base::BindRepeating(&AerieSidebarView::OnRowFocused,
                                            base::Unretained(this));
    callbacks.drag_begin = base::BindRepeating(
        &AerieSidebarView::OnRowDragBegin, base::Unretained(this), id);
    callbacks.drag_update = base::BindRepeating(
        &AerieSidebarView::OnRowDragUpdate, base::Unretained(this), id);
    callbacks.drag_end = base::BindRepeating(&AerieSidebarView::OnRowDragEnd,
                                             base::Unretained(this), id);
    auto* view = list_->AddChildView(std::make_unique<AerieSidebarRowView>(
        std::move(callbacks), palette_));
    next.push_back({id, view});
    added.push_back(view);
  }

  // A removed row that has keyboard focus hands it to the row that takes its
  // place (HandleListKey: the same index, or the new last row).
  int refocus_index = -1;
  for (auto& [id, view] : existing) {
    if (view->HasFocus() || id == refocus_after_close_id_) {
      refocus_index =
          aerie_sidebar::HandleListKey(old_count, old_index[id],
                                       aerie_sidebar::ListKey::kDelete)
              .focus_index;
    }
    if (animate) {
      closing_rows_.push_back(view);
      view->PlayClose(
          prefs, OuterEdgeSign(),
          base::BindOnce(
              [](base::WeakPtr<AerieSidebarView> self,
                 AerieSidebarRowView* row) {
                // Not from inside the row's own animation.
                base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                    FROM_HERE,
                    base::BindOnce(
                        [](base::WeakPtr<AerieSidebarView> self,
                           AerieSidebarRowView* row) {
                          if (self) {
                            self->RemoveClosedRow(row);
                          }
                        },
                        self, row));
              },
              weak_factory_.GetWeakPtr(), view.get()));
    } else {
      list_->RemoveChildViewT(view.get());
    }
  }
  rows_ = std::move(next);
  if (refocus_index >= 0) {
    refocus_after_close_id_.clear();
  }

  // Put the live rows in order. A row that is still closing keeps its place.
  {
    std::vector<views::View*> order;
    size_t next_live = 0;
    std::vector<views::View*> children(list_->children().begin(),
                                       list_->children().end());
    for (views::View* child : children) {
      const bool closing =
          std::find(closing_rows_.begin(), closing_rows_.end(), child) !=
          closing_rows_.end();
      order.push_back(closing ? child : rows_[next_live++].view.get());
    }
    for (size_t i = 0; i < order.size(); ++i) {
      list_->ReorderChildView(order[i], i);
    }
  }
  UpdateRows();
  UpdateTabStop();
  list_->InvalidateLayout();

  if (refocus_index >= 0 && !rows_.empty()) {
    FocusRow(std::min(refocus_index, static_cast<int>(rows_.size()) - 1));
  }
  if (animate) {
    for (AerieSidebarRowView* view : added) {
      view->PlayOpen(prefs);
    }
  }
}

bool AerieSidebarView::OnRowDragBegin(const std::string& tab_id) {
  if (drag_.active || rows_.size() < 2 || !closing_rows_.empty()) {
    return false;
  }
  for (const Row& row : rows_) {
    if (row.view->height_animating() || row.view->is_closing()) {
      return false;
    }
  }
  const int source = IndexOfRow(tab_id);
  if (source < 0) {
    return false;
  }
  DragState drag;
  drag.active = true;
  drag.tab_id = tab_id;
  drag.source = source;
  drag.target = source;
  for (const Row& row : rows_) {
    drag.slots.push_back({row.view->y(), row.view->height()});
  }
  drag_ = std::move(drag);
  rows_[source].view->BeginLift(browser_->GetProfile()->GetPrefs());
  return true;
}

void AerieSidebarView::OnRowDragUpdate(const std::string& tab_id, int dy) {
  if (!drag_.active || drag_.tab_id != tab_id) {
    return;
  }
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  const int offset = aerie_sidebar::ClampDragOffset(drag_.slots, drag_.source, dy);
  rows_[drag_.source].view->SetDragOffset(offset);
  const int target =
      aerie_sidebar::DragTargetIndex(drag_.slots, drag_.source, offset);
  if (target == drag_.target) {
    return;
  }
  drag_.target = target;
  // Reduced motion: no shift. The rows stay, and the drop moves the tab.
  if (aerie::motion::ShouldReduceMotion(prefs)) {
    return;
  }
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (static_cast<int>(i) != drag_.source) {
      rows_[i].view->ShiftTo(
          aerie_sidebar::DragShift(drag_.slots, drag_.source, target,
                                   static_cast<int>(i)),
          prefs);
    }
  }
}

void AerieSidebarView::OnRowDragEnd(const std::string& tab_id,
                                    int dy,
                                    bool cancelled,
                                    bool outside) {
  if (!drag_.active || drag_.tab_id != tab_id) {
    return;
  }
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  DragState drag = std::move(drag_);
  drag_ = DragState();
  const int offset = aerie_sidebar::ClampDragOffset(drag.slots, drag.source, dy);

  // Dropped outside the window: the tab goes to a window of its own.
  if (!cancelled && outside) {
    for (const Row& row : rows_) {
      row.view->ResetDrag();
    }
    TabStripModel* model = browser_->GetTabStripModel();
    tabs::TabInterface* tab = FindTab(tab_id);
    TabStripModelDelegate* delegate = model->delegate();
    if (tab && delegate) {
      const std::vector<int> indices = {model->GetIndexOfTab(tab)};
      if (delegate->CanMoveTabsToWindow(indices)) {
        delegate->MoveTabsToNewWindow(indices);
      }
    }
    return;
  }

  const int target =
      cancelled ? drag.source
                : aerie_sidebar::DragTargetIndex(drag.slots, drag.source, offset);
  AerieSidebarRowView* dragged = rows_[drag.source].view;
  if (target == drag.source) {
    // Nothing moved: the row settles back and the others go back to rest.
    dragged->Settle(offset, prefs);
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (static_cast<int>(i) != drag.source &&
          rows_[i].view->has_drag_layers()) {
        rows_[i].view->ShiftTo(0, prefs);
      }
    }
    return;
  }

  // Dropped in another slot. The rows that shifted take their new places in
  // the layout, which is the position the shift already showed, so they reset
  // with no jump; the dropped row settles from where it was released.
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (static_cast<int>(i) != drag.source) {
      rows_[i].view->ResetDrag();
    }
  }
  MoveTodayRow(drag.source, target);
  dragged->Settle(
      aerie_sidebar::SettleOffset(drag.slots, drag.source, target, offset),
      prefs);
}

void AerieSidebarView::CancelDrag() {
  const bool was_active = drag_.active;
  drag_ = DragState();
  if (!was_active) {
    return;
  }
  for (const Row& row : rows_) {
    row.view->ResetDrag();
  }
}

void AerieSidebarView::MoveTodayRow(int source, int target) {
  tabs::TabInterface* from = FindTab(rows_[source].tab_id);
  tabs::TabInterface* to = FindTab(rows_[target].tab_id);
  if (!from || !to) {
    return;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  model->MoveWebContentsAt(model->GetIndexOfTab(from), model->GetIndexOfTab(to),
                           /*select_after_move=*/false);
  // The strip told the Tab Model (TabStripAdapter), which told the Today list.
  // The rows are put in the new order now, not in a task, so that the dropped
  // row can settle from where it was released, so the layout runs now too.
  SyncRows();
  list_->DeprecatedLayoutImmediately();
}

void AerieSidebarView::RemoveClosedRow(AerieSidebarRowView* row) {
  auto it = std::find(closing_rows_.begin(), closing_rows_.end(), row);
  if (it == closing_rows_.end()) {
    return;
  }
  closing_rows_.erase(it);
  list_->RemoveChildViewT(row);
  list_->InvalidateLayout();
}

bool AerieSidebarView::CanAnimateRows() const {
  // No animation before the window is interactive, and none while a session
  // comes back: those rows appear as they are.
  return GetWidget() && GetWidget()->IsVisible() &&
         !SessionRestore::IsRestoring(browser_->GetProfile());
}

int AerieSidebarView::OuterEdgeSign() const {
  const views::Widget* widget = GetWidget();
  if (!widget) {
    return -1;
  }
  gfx::Point middle(width() / 2, 0);
  views::View::ConvertPointToWidget(this, &middle);
  return middle.x() > widget->GetRootView()->width() / 2 ? 1 : -1;
}

int AerieSidebarView::IndexOfRow(const std::string& tab_id) const {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].tab_id == tab_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

AerieSidebarRowView* AerieSidebarView::row_at(size_t index) const {
  return index < rows_.size() ? rows_[index].view.get() : nullptr;
}

std::string AerieSidebarView::tab_id_at(size_t index) const {
  return index < rows_.size() ? rows_[index].tab_id : std::string();
}

AerieSidebarRowView* AerieSidebarView::closing_row_at(size_t index) const {
  return index < closing_rows_.size() ? closing_rows_[index].get() : nullptr;
}

AerieSidebarRowView* AerieSidebarView::tab_stop_row() const {
  for (const Row& row : rows_) {
    if (row.view->is_tab_stop()) {
      return row.view;
    }
  }
  return nullptr;
}

void AerieSidebarView::OnRowKey(const std::string& tab_id,
                                aerie_sidebar::ListKey key) {
  const int focused = IndexOfRow(tab_id);
  if (focused < 0) {
    return;
  }
  const aerie_sidebar::ListKeyResult result = aerie_sidebar::HandleListKey(
      static_cast<int>(rows_.size()), focused, key);
  if (result.close_focused) {
    // Focus moves when the row has gone (SyncRows), to the row that takes its
    // place.
    refocus_after_close_id_ = tab_id;
    CloseTab(tab_id);
    return;
  }
  FocusRow(result.focus_index);
}

void AerieSidebarView::OnRowFocused() {
  refocus_after_close_id_.clear();
  UpdateTabStop();
}

void AerieSidebarView::UpdateTabStop() {
  // The row that has focus is the stop; with focus outside the list it is the
  // selected row, so that Tab enters the list at the open tab; else the first.
  AerieSidebarRowView* stop = nullptr;
  for (const Row& row : rows_) {
    if (row.view->HasFocus()) {
      stop = row.view;
      break;
    }
  }
  if (!stop) {
    for (const Row& row : rows_) {
      if (row.view->selected()) {
        stop = row.view;
        break;
      }
    }
  }
  if (!stop && !rows_.empty()) {
    stop = rows_.front().view;
  }
  for (const Row& row : rows_) {
    row.view->SetTabStop(row.view == stop);
  }
}

void AerieSidebarView::FocusRow(int index) {
  if (index < 0 || index >= static_cast<int>(rows_.size())) {
    return;
  }
  AerieSidebarRowView* target = rows_[index].view;
  // Roving: only the row that is the stop can be focused by RequestFocus, so
  // the stop moves first.
  for (const Row& row : rows_) {
    row.view->SetTabStop(row.view == target);
  }
  target->RequestFocus();
  target->ScrollViewToVisible();
}

void AerieSidebarView::FocusTabStop() {
  if (rows_.empty()) {
    return;
  }
  UpdateTabStop();
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].view->is_tab_stop()) {
      FocusRow(static_cast<int>(i));
      return;
    }
  }
}

void AerieSidebarView::GetAccessiblePanes(std::vector<views::View*>* panes) {
  if (!GetVisible()) {
    return;
  }
  panes->push_back(top_row_);
  panes->push_back(pill_);
  panes->push_back(list_);
  if (adopted_toolbar_) {
    panes->push_back(adopted_toolbar_);
  }
}

void AerieSidebarView::UpdateRows() {
  for (const Row& row : rows_) {
    UpdateRow(row);
  }
}

void AerieSidebarView::UpdateRow(const Row& row) {
  tabs::TabInterface* tab = FindTab(row.tab_id);
  if (!tab) {
    return;
  }
  row.view->SetTitle(tab->GetTitle());
  row.view->SetFavicon(FaviconFor(tab, palette_.text_muted));
  row.view->SetSelected(tab->IsActivated());
}

void AerieSidebarView::SelectTab(const std::string& tab_id) {
  if (tabs::TabInterface* tab = FindTab(tab_id)) {
    TabStripModel* model = browser_->GetTabStripModel();
    model->ActivateTabAt(model->GetIndexOfTab(tab));
  }
}

void AerieSidebarView::CloseTab(const std::string& tab_id) {
  if (tabs::TabInterface* tab = FindTab(tab_id)) {
    tab->Close();
  }
}

void AerieSidebarView::ScheduleSync() {
  if (sync_pending_) {
    return;
  }
  sync_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<AerieSidebarView> self) {
                       if (self) {
                         self->sync_pending_ = false;
                         self->SyncRows();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void AerieSidebarView::OnRowInserted(int index, const std::string& tab_id) {
  ScheduleSync();
}

void AerieSidebarView::OnRowRemoved(int index, const std::string& tab_id) {
  ScheduleSync();
}

void AerieSidebarView::OnRowsReset() {
  ScheduleSync();
}

void AerieSidebarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // A tab that comes or goes shows up when the Tab Model says so; a new
  // selection only changes which row is marked.
  if (change.type() != TabStripModelChange::kSelectionOnly) {
    ScheduleSync();
  }
  if (selection.active_tab_changed()) {
    UpdateRows();
    UpdateTabStop();
  }
}

void AerieSidebarView::OnTabChangedAt(tabs::TabInterface* tab,
                                      TabChangeType change_type) {
  UpdateRows();
}

BEGIN_METADATA(AerieSidebarView)
END_METADATA
