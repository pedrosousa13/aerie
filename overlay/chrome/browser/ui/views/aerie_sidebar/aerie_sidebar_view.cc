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
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"

namespace {

ui::ImageModel FaviconFor(tabs::TabInterface* tab) {
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
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                        ui::kColorSysOnSurface,
                                        aerie::tokens::kSizeIcon);
}

}  // namespace

AerieSidebarView::AerieSidebarView(BrowserWindowInterface* browser,
                                   base::RepeatingClosure new_tab)
    : browser_(browser), new_tab_(std::move(new_tab)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(aerie::tokens::kSpace8, aerie::tokens::kSpace8),
      aerie::tokens::kSpace4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  GetViewAccessibility().SetRole(ax::mojom::Role::kNavigation);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NAME));

  // New Tab row.
  auto* new_tab_row = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](AerieSidebarView* self) { self->new_tab_.Run(); },
          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_TAB)));
  new_tab_row->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kAddIcon, ui::kColorSysOnSurface,
                                     aerie::tokens::kSizeIcon));
  new_tab_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  new_tab_row->SetImageLabelSpacing(aerie::tokens::kSpace8);
  new_tab_row->SetPreferredSize(
      gfx::Size(0, aerie::tokens::kSizeRowHeightComfortable));
  new_tab_row->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_TAB));

  // Today.
  auto* today_heading = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TODAY),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  today_heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  today_heading->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  today_heading->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(aerie::tokens::kSpace8, aerie::tokens::kSpace8, 0, 0)));

  scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kDisabled));
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  list->SetBetweenChildSpacing(aerie::tokens::kSpace2);
  list->GetViewAccessibility().SetRole(ax::mojom::Role::kTree);
  list->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TODAY));
  list_ = scroll_->SetContents(std::move(list));
  layout->SetFlexForView(scroll_, 1);

  browser_->GetTabStripModel()->AddObserver(this);

  if (auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
          browser_->GetProfile())) {
    today_list_ = std::make_unique<aerie_sidebar::TodayList>(service->model(),
                                                             this);
  }
  SyncRows();
  ScheduleSync();
}

AerieSidebarView::~AerieSidebarView() = default;

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
  std::vector<std::string> wanted;
  if (today_list_) {
    for (const std::string& id : today_list_->rows()) {
      if (FindTab(id)) {
        wanted.push_back(id);
      }
    }
  }

  // Keep the views of rows that stay, drop the others, add the new ones, and
  // put them in order.
  std::map<std::string, raw_ptr<AerieSidebarRowView>> existing;
  for (const Row& row : rows_) {
    existing[row.tab_id] = row.view;
  }
  std::vector<Row> next;
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
    auto* view = list_->AddChildView(
        std::make_unique<AerieSidebarRowView>(std::move(callbacks)));
    next.push_back({id, view});
  }
  for (auto& [id, view] : existing) {
    list_->RemoveChildViewT(view.get());
  }
  rows_ = std::move(next);
  for (size_t i = 0; i < rows_.size(); ++i) {
    list_->ReorderChildView(rows_[i].view, i);
  }
  UpdateRows();
  list_->InvalidateLayout();
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
  row.view->SetFavicon(FaviconFor(tab));
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
  }
}

void AerieSidebarView::OnTabChangedAt(tabs::TabInterface* tab,
                                      TabChangeType change_type) {
  UpdateRows();
}

BEGIN_METADATA(AerieSidebarView)
END_METADATA
