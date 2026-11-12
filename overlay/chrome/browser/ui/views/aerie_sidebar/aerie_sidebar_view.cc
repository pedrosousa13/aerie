// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_sheet.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_group.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_popover.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_top_row.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_drag.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_tree.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/sleep_summary.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace {

// How long after the sidebar is built the sleep controller is created (issue
// #179: nothing at startup).
constexpr base::TimeDelta kSleepControllerDelay = base::Seconds(5);

// How many pinned rows show before the Pinned list scrolls (`size-pinned-max-rows`).
constexpr int kPinnedMaxVisibleRows = aerie::tokens::kSizePinnedMaxRows;

// The tooltip of the close button of a pinned row: it names the action.
int CloseTooltipMessageId(aerie_tab_model::PinnedCloseAction action) {
  switch (action) {
    case aerie_tab_model::PinnedCloseAction::kResetSleepSwitch:
      return IDS_AERIE_SIDEBAR_CLOSE_RESET_SLEEP;
    case aerie_tab_model::PinnedCloseAction::kSleepSwitch:
      return IDS_AERIE_SIDEBAR_CLOSE_SLEEP;
    case aerie_tab_model::PinnedCloseAction::kUnpinClose:
      return IDS_AERIE_SIDEBAR_CLOSE_CLOSE;
  }
}

// "Can't nest deeper than 5 levels": the tooltip of New folder and of Move to
// new folder when the folder they would make is too deep.
std::u16string TooDeepTooltip() {
  return l10n_util::GetStringFUTF16(
      IDS_AERIE_SIDEBAR_FOLDER_TOO_DEEP,
      base::NumberToString16(aerie_tab_model::kMaxFolderDepth));
}

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

// The precedence for the titles the sidebar shows (issue #145): the manual name
// first, then the page title. #146 and #147 add providers to this list.
const aerie_tab_model::TitleResolver& SharedTitleResolver() {
  static const base::NoDestructor<aerie_tab_model::TitleResolver> resolver(
      aerie_tab_model::TitleResolver::CreateDefault());
  return *resolver;
}

}  // namespace

AerieSidebarView::ShownTitle AerieSidebarView::ShownTitleFor(
    tabs::TabInterface* tab,
    const aerie_tab_model::TabRecord* record) const {
  const std::u16string page_title = tab->GetTitle();
  ShownTitle shown;
  if (!record) {
    shown.text = page_title;
    return shown;
  }
  const aerie_tab_model::ResolvedTitle resolved =
      SharedTitleResolver().ResolveTitle(*record,
                                         base::UTF16ToUTF8(page_title));
  if (resolved.source == aerie_tab_model::TitleSource::kPage) {
    shown.text = page_title;
    return shown;
  }
  shown.text = base::UTF8ToUTF16(resolved.text);
  // Only a manual name has a pencil in M2; #146 and #147 give the other sources
  // their own "original title" tooltip.
  if (resolved.source == aerie_tab_model::TitleSource::kManual) {
    shown.renamed = true;
    shown.tooltip =
        l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_RENAMED_TOOLTIP,
                                   page_title) +
        u"\n" + l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAMED_PRECEDENCE);
  }
  return shown;
}

// The Favorites and Pinned lists tell the view that they changed; the view
// syncs every section in one task.
class AerieSidebarView::SectionNotifier
    : public aerie_sidebar::TodayList::Delegate {
 public:
  explicit SectionNotifier(AerieSidebarView* view) : view_(view) {}
  void OnRowInserted(int, const std::string&) override { view_->ScheduleSync(); }
  void OnRowRemoved(int, const std::string&) override { view_->ScheduleSync(); }
  void OnRowsReset() override { view_->ScheduleSync(); }

 private:
  const raw_ptr<AerieSidebarView> view_;
};

AerieSidebarView::AerieSidebarView(BrowserWindowInterface* browser,
                                   base::RepeatingClosure new_tab)
    : browser_(browser), new_tab_(std::move(new_tab)) {
  if (auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
          browser_->GetProfile())) {
    model_ = service->model();
    model_->AddObserver(this);
    auto_file_ = service->auto_file();
    if (auto_file_) {
      auto_file_->AddObserver(this);
    }
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

  // Favorites (issue #180): a grid of tiles, hidden while it has none.
  favorites_grid_ = AddChildView(std::make_unique<AerieSidebarFavoritesGrid>());
  favorites_grid_->SetFocusHandler(base::BindRepeating(
      &AerieSidebarView::FocusTileStop, base::Unretained(this)));
  favorites_grid_->GetViewAccessibility().SetRole(ax::mojom::Role::kGrid);
  favorites_grid_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FAVORITES));
  favorites_grid_->SetVisible(false);

  // Pinned (issue #180): a flat list of rows above the New Tab row. It scrolls
  // on its own after a few rows, so that twenty pinned tabs do not push the
  // Today list off the sidebar.
  constexpr int kPinnedRingRoom = static_cast<int>(
      aerie::tokens::kSizeFocusRingOffset + aerie::tokens::kSizeFocusRingWidth);
  pinned_scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kDisabled));
  pinned_scroll_->SetBackgroundColor(std::nullopt);
  pinned_scroll_->ClipHeightTo(
      0, kPinnedMaxVisibleRows *
                 (aerie::tokens::kSizeRowHeightComfortable + kPinnedRingRoom) +
             2 * kPinnedRingRoom);
  pinned_scroll_->SetDrawOverflowIndicator(false);
  pinned_scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto pinned = std::make_unique<AerieSidebarTodayTree>();
  pinned->SetOrientation(views::BoxLayout::Orientation::kVertical);
  pinned->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  pinned->SetBetweenChildSpacing(kPinnedRingRoom);
  pinned->SetInsideBorderInsets(gfx::Insets(kPinnedRingRoom));
  pinned->SetFocusHandler(base::BindRepeating(
      &AerieSidebarView::FocusPinnedTabStop, base::Unretained(this)));
  pinned->GetViewAccessibility().SetRole(ax::mojom::Role::kTree);
  pinned->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_PINNED));
  pinned_list_ = pinned_scroll_->SetContents(std::move(pinned));
  pinned_scroll_->SetVisible(false);

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
  // The heading, and at its end the number of sleeping tabs with an estimate of
  // the memory they freed (issue #179). The estimate's tooltip says it is one.
  auto* today_header = AddChildView(std::make_unique<views::BoxLayoutView>());
  today_header->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  today_heading_ = today_header->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TODAY),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  today_heading_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  today_heading_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(aerie::tokens::kSpace8, aerie::tokens::kSpace8, 0, 0)));
  today_header->SetFlexForView(today_heading_, 1);
  sleeping_summary_ = today_header->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  sleeping_summary_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  sleeping_summary_->SetElideBehavior(gfx::ELIDE_TAIL);
  sleeping_summary_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(aerie::tokens::kSpace8, 0, 0, aerie::tokens::kSpace8)));
  sleeping_summary_->SetVisible(false);

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
  // Right-click on the empty part of a section offers New folder.
  list_->set_context_menu_controller(this);
  pinned_list_->set_context_menu_controller(this);
  scroll_->set_context_menu_controller(this);
  pinned_scroll_->set_context_menu_controller(this);
  today_header->set_context_menu_controller(this);
  layout->SetFlexForView(scroll_, 1);

  // The toast: one short message, polite for a screen reader (docs/sidebar-
  // design.md, "Roles and names").
  auto card = std::make_unique<views::BoxLayoutView>();
  card->SetOrientation(views::BoxLayout::Orientation::kVertical);
  card->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  toast_ = card->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  toast_->SetMultiLine(true);
  toast_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  toast_->GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  toast_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
      aerie::tokens::kSpace8, aerie::tokens::kSpace12)));
  // The buttons of a toast that offers a choice (issue #139).
  auto actions = std::make_unique<views::BoxLayoutView>();
  actions->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  actions->SetBetweenChildSpacing(aerie::tokens::kSpace8);
  actions->SetInsideBorderInsets(gfx::Insets::TLBR(
      0, aerie::tokens::kSpace8, aerie::tokens::kSpace8, aerie::tokens::kSpace8));
  toast_undo_ = actions->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&AerieSidebarView::UndoFiling, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FILED_UNDO)));
  toast_dont_file_ = actions->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&AerieSidebarView::DontFileThisSite,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FILED_DONT_FILE_SITE)));
  actions->SetVisible(false);
  toast_actions_ = card->AddChildView(std::move(actions));
  card->SetVisible(false);
  toast_card_ = AddChildView(std::move(card));

  // Bottom bar: a placeholder until the Archive button and the Workspace
  // switcher come. The toolbar's remaining buttons (extensions, profile, menu)
  // sit in it so that they stay reachable.
  bottom_bar_ = AddChildView(std::make_unique<views::View>());
  bottom_bar_->SetLayoutManager(std::make_unique<views::FillLayout>());

  browser_->GetTabStripModel()->AddObserver(this);

  if (model_) {
    today_list_ = std::make_unique<aerie_sidebar::TodayList>(model_, this);
    favorites_notifier_ = std::make_unique<SectionNotifier>(this);
    favorites_list_ = std::make_unique<aerie_sidebar::TodayList>(
        model_, favorites_notifier_.get(), aerie_tab_model::Section::kFavorite);
    pinned_notifier_ = std::make_unique<SectionNotifier>(this);
    pinned_list_model_ = std::make_unique<aerie_sidebar::TodayList>(
        model_, pinned_notifier_.get(), aerie_tab_model::Section::kPinned);
  }
  // The close tooltip of a pinned row names what the setting does now.
  pref_registrar_.Init(browser_->GetProfile()->GetPrefs());
  pref_registrar_.Add(
      aerie_tab_model::kPinnedClose,
      base::BindRepeating(&AerieSidebarView::UpdateRows, base::Unretained(this)));
  ApplyPalette();
  SyncAll();
  ScheduleSync();

  // The sleep controller is built a few seconds after the window opens, not at
  // startup (issue #179: no startup cost). Asking for it builds it.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<AerieSidebarView> self) {
            if (self) {
              aerie_tab_model::SleepControllerFactory::GetForProfile(
                  self->browser_->GetProfile());
            }
          },
          weak_factory_.GetWeakPtr()),
      kSleepControllerDelay);
}

AerieSidebarView::~AerieSidebarView() {
  // The popover of a tile goes with the sidebar, and keeps nothing.
  if (rename_popover_) {
    rename_popover_->CancelRename();
  }
  if (model_) {
    model_->RemoveObserver(this);
  }
  if (auto_file_) {
    auto_file_->RemoveObserver(this);
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
  sleeping_summary_->SetEnabledColor(palette_.text_muted);
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
  for (const Row& row : pinned_rows_) {
    row.view->SetPalette(palette_);
  }
  for (const Row& row : collapsing_rows_) {
    row.view->SetPalette(palette_);
  }
  for (const Tile& tile : tiles_) {
    tile.view->SetPalette(palette_);
  }
  toast_->SetEnabledColor(palette_.text);
  toast_card_->SetBackground(views::CreateRoundedRectBackground(
      palette_.row_selected, aerie::tokens::kRadiusMd));
  toast_undo_->SetEnabledTextColors(palette_.text);
  toast_dont_file_->SetEnabledTextColors(palette_.text);
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

std::map<std::string, tabs::TabInterface*> AerieSidebarView::BuildTabLookup()
    const {
  std::map<std::string, tabs::TabInterface*> lookup;
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser_->GetProfile());
  if (!service || !service->adapter()) {
    return lookup;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    const std::string id = service->adapter()->RecordIdForTab(tab);
    if (!id.empty()) {
      lookup[id] = tab;
    }
  }
  return lookup;
}

tabs::TabInterface* AerieSidebarView::FindTab(const std::string& tab_id) const {
  // Inside a sync or a refresh the map is built once for all the rows.
  if (tab_lookup_) {
    auto it = tab_lookup_->find(tab_id);
    return it == tab_lookup_->end() ? nullptr : it->second;
  }
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

std::vector<aerie_tab_model::TreeRow> AerieSidebarView::WantedRows(
    aerie_tab_model::Section section) const {
  if (!model_) {
    return {};
  }
  return aerie_tab_model::BuildVisibleRows(
      *model_, section,
      [this](const std::string& id) { return FindTab(id) != nullptr; });
}

bool AerieSidebarView::TreeChanged(
    const std::vector<aerie_tab_model::TreeRow>& wanted,
    const std::vector<Row>& rows) const {
  if (wanted.size() != rows.size()) {
    return true;
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    if (wanted[i].id != rows[i].tab_id) {
      return true;
    }
  }
  return false;
}

AerieSidebarRowView* AerieSidebarView::MakeRow(
    const aerie_tab_model::TreeRow& entry,
    AerieSidebarTodayTree* list,
    bool today) {
  const std::string id = entry.id;
  AerieSidebarRowView::Callbacks callbacks;
  if (entry.is_folder) {
    // A folder row: a click, Enter or Space opens or shuts it. It cannot close
    // and, until issue #182, it cannot be dragged.
    callbacks.select = base::BindRepeating(&AerieSidebarView::ToggleFolder,
                                           base::Unretained(this), id);
    callbacks.close = base::DoNothing();
  } else {
    callbacks.select = base::BindRepeating(&AerieSidebarView::SelectTab,
                                           base::Unretained(this), id);
    callbacks.close = base::BindRepeating(&AerieSidebarView::CloseTab,
                                          base::Unretained(this), id);
  }
  callbacks.key = base::BindRepeating(&AerieSidebarView::OnTreeRowKey,
                                      base::Unretained(this), today, id);
  callbacks.focused = today ? base::BindRepeating(
                                  &AerieSidebarView::OnRowFocused,
                                  base::Unretained(this))
                            : base::BindRepeating(
                                  &AerieSidebarView::OnPinnedRowFocused,
                                  base::Unretained(this));
  if (today && !entry.is_folder) {
    callbacks.drag_begin = base::BindRepeating(
        &AerieSidebarView::OnRowDragBegin, base::Unretained(this), id);
    callbacks.drag_update = base::BindRepeating(
        &AerieSidebarView::OnRowDragUpdate, base::Unretained(this), id);
    callbacks.drag_end = base::BindRepeating(&AerieSidebarView::OnRowDragEnd,
                                             base::Unretained(this), id);
  }
  callbacks.context_menu = base::BindRepeating(
      &AerieSidebarView::ShowRowContextMenu, base::Unretained(this), id);
  if (!today && !entry.is_folder) {
    callbacks.back_to_pinned = base::BindRepeating(
        &AerieSidebarView::GoBackToPinnedPage, base::Unretained(this), id);
  }
  callbacks.begin_rename =
      base::BindRepeating(base::IgnoreResult(&AerieSidebarView::StartRename),
                          base::Unretained(this), id);
  callbacks.commit_rename = base::BindRepeating(
      &AerieSidebarView::CommitRename, base::Unretained(this), id);
  callbacks.reset_name = base::BindRepeating(&AerieSidebarView::ResetName,
                                             base::Unretained(this), id);
  if (entry.is_folder) {
    callbacks.edit_folder = base::BindRepeating(
        &AerieSidebarView::OpenFolderSheet, base::Unretained(this), id);
  }
  auto* view = list->AddChildView(
      std::make_unique<AerieSidebarRowView>(std::move(callbacks), palette_));
  if (entry.is_folder) {
    view->MakeFolderRow();
  }
  return view;
}

AerieSidebarView::Reconciled AerieSidebarView::ReconcileRows(
    const std::vector<Row>& current,
    const std::vector<aerie_tab_model::TreeRow>& wanted,
    AerieSidebarTodayTree* list,
    bool today) {
  Reconciled result;
  // Keep the views of rows that stay, drop the others, add the new ones.
  std::map<std::string, Row> existing;
  for (size_t i = 0; i < current.size(); ++i) {
    existing[current[i].tab_id] = current[i];
    result.old_index[current[i].tab_id] = static_cast<int>(i);
  }
  // A folder that was shutting and is opened again takes its rows back: they go
  // on from where they got to.
  if (toggle_.active && toggle_.expand && group_ && group_->running() &&
      group_->folder_id() == toggle_.folder_id) {
    for (auto it = collapsing_rows_.begin(); it != collapsing_rows_.end();) {
      const bool wanted_again =
          it->view->parent() == list &&
          std::any_of(wanted.begin(), wanted.end(),
                      [&](const aerie_tab_model::TreeRow& entry) {
                        return entry.id == it->tab_id;
                      });
      if (wanted_again) {
        it->view->GetViewAccessibility().SetIsIgnored(false);
        existing[it->tab_id] = *it;
        it = collapsing_rows_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (const aerie_tab_model::TreeRow& entry : wanted) {
    auto it = existing.find(entry.id);
    if (it != existing.end()) {
      Row row = it->second;
      row.item_count = entry.item_count;
      row.view->SetLevel(entry.level);
      result.next.push_back(row);
      existing.erase(it);
      continue;
    }
    AerieSidebarRowView* view = MakeRow(entry, list, today);
    view->SetLevel(entry.level);
    result.next.push_back(
        {entry.id, view, entry.is_folder, entry.item_count});
    if (toggle_.active && toggle_.expand &&
        IsInsideFolder(toggle_.folder_id, entry.id)) {
      toggle_revealed_.push_back(view);
    } else {
      result.added.push_back(view);
    }
  }
  for (auto& [id, row] : existing) {
    result.gone.push_back(row);
  }
  return result;
}

bool AerieSidebarView::KeepAsCollapsing(const Row& row) {
  // A row that goes because its folder shut keeps its place until the collapse
  // has played, then goes (issue #181). Keyboard focus in it went to the folder
  // row (SetFolderExpanded).
  if (!CanAnimateRows() || !toggle_.active || toggle_.expand ||
      !IsInsideFolder(toggle_.folder_id, row.tab_id)) {
    return false;
  }
  collapsing_rows_.push_back(row);
  toggle_collapsing_.push_back(row.view);
  row.view->SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  row.view->GetViewAccessibility().SetIsIgnored(true);
  return true;
}

void AerieSidebarView::OrderRows(AerieSidebarTodayTree* list,
                                 const std::vector<Row>& rows) {
  // The live rows in order; a row that still closes or collapses keeps its
  // place.
  std::vector<views::View*> order;
  size_t next_live = 0;
  const std::vector<views::View*> children(list->children().begin(),
                                           list->children().end());
  for (views::View* child : children) {
    const bool keeps_place =
        std::find(closing_rows_.begin(), closing_rows_.end(), child) !=
            closing_rows_.end() ||
        std::any_of(collapsing_rows_.begin(), collapsing_rows_.end(),
                    [child](const Row& row) { return row.view == child; });
    order.push_back(keeps_place ? child : rows[next_live++].view.get());
  }
  for (size_t i = 0; i < order.size(); ++i) {
    list->ReorderChildView(order[i], i);
  }
}

void AerieSidebarView::SyncRows(
    const std::vector<aerie_tab_model::TreeRow>& wanted) {
  // The rows' slots change: a drag in progress would move rows that are not
  // where it measured them.
  if (drag_.active) {
    CancelDrag();
  }
  const bool animate = CanAnimateRows();
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  const int old_count = static_cast<int>(rows_.size());
  Reconciled result = ReconcileRows(rows_, wanted, list_, /*today=*/true);

  // A removed row that has keyboard focus hands it to the row that takes its
  // place (HandleListKey: the same index, or the new last row).
  int refocus_index = -1;
  for (Row& row : result.gone) {
    if (KeepAsCollapsing(row)) {
      continue;
    }
    AerieSidebarRowView* view = row.view;
    // A tab that went to Pinned or Favorites (issue #180) leaves the list at
    // once: the FLIP motion moves its new row from here, so it does not play
    // the close motion as well.
    const aerie_tab_model::TabRecord* moved_record =
        model_ ? model_->GetTab(row.tab_id) : nullptr;
    const bool changed_section =
        moved_record &&
        moved_record->section != aerie_tab_model::Section::kToday;
    if (view->HasFocus() || row.tab_id == refocus_after_close_id_) {
      refocus_index =
          aerie_sidebar::HandleListKey(old_count,
                                       result.old_index[row.tab_id],
                                       aerie_sidebar::ListKey::kDelete)
              .focus_index;
    }
    if (animate && !changed_section) {
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
              weak_factory_.GetWeakPtr(), view));
    } else {
      list_->RemoveChildViewT(view);
    }
  }
  rows_ = std::move(result.next);
  today_tree_rows_ = wanted;
  if (refocus_index >= 0) {
    refocus_after_close_id_.clear();
  }
  OrderRows(list_, rows_);
  UpdateRows();
  UpdateTabStop();
  list_->InvalidateLayout();

  if (refocus_index >= 0 && !rows_.empty()) {
    FocusRow(std::min(refocus_index, static_cast<int>(rows_.size()) - 1));
  }
  if (animate) {
    for (AerieSidebarRowView* view : result.added) {
      view->PlayOpen(prefs);
    }
  }
}

void AerieSidebarView::SyncAll() {
  // One pass over the strip serves every row (issue #181: 300 tabs).
  const std::map<std::string, tabs::TabInterface*> lookup = BuildTabLookup();
  base::AutoReset<raw_ptr<const std::map<std::string, tabs::TabInterface*>>>
      use_lookup(&tab_lookup_, &lookup);
  // The two trees are walked once for the whole sync.
  const std::vector<aerie_tab_model::TreeRow> today =
      WantedRows(aerie_tab_model::Section::kToday);
  const std::vector<aerie_tab_model::TreeRow> pinned =
      WantedRows(aerie_tab_model::Section::kPinned);
  // A folder motion that runs goes on only when the sync is the turn-around of
  // the same folder or changes no row. Anything else ends it at once, so that
  // no row is moved under it.
  if (group_ && group_->running()) {
    const bool turn_around = toggle_.active &&
                             group_->folder_id() == toggle_.folder_id;
    if (!turn_around &&
        (toggle_.active || TreeChanged(today, rows_) ||
         TreeChanged(pinned, pinned_rows_))) {
      group_->Finish();
    }
  }
  // FLIP (issue #180): when a tab changed section, remember where every row is,
  // sync, lay out, and move each row from where it was.
  const bool flip = flip_pending_;
  flip_pending_ = false;
  const std::string moved_id = std::move(flip_moved_id_);
  flip_moved_id_.clear();
  const bool play_flip =
      flip && CanAnimateRows() &&
      !aerie::motion::ShouldReduceMotion(browser_->GetProfile()->GetPrefs());
  std::map<std::string, gfx::Point> before = std::move(flip_before_);
  flip_before_.clear();
  SyncFavorites();
  SyncPinned(pinned);
  SyncRows(today);
  StartToggleGroup();
  if (play_flip) {
    DeprecatedLayoutImmediately();
    PlayFlip(before, moved_id);
  }
}

std::map<std::string, gfx::Point> AerieSidebarView::CapturePositions() const {
  std::map<std::string, gfx::Point> positions;
  auto record = [&](const std::string& id, views::View* view) {
    gfx::Point origin;
    views::View::ConvertPointToTarget(view, this, &origin);
    positions[id] = origin;
  };
  for (const Tile& tile : tiles_) {
    record(tile.tab_id, tile.view);
  }
  for (const Row& row : pinned_rows_) {
    record(row.tab_id, row.view);
  }
  for (const Row& row : rows_) {
    record(row.tab_id, row.view);
  }
  return positions;
}

void AerieSidebarView::PlayFlip(const std::map<std::string, gfx::Point>& before,
                                const std::string& moved_id) {
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  auto delta_of = [&](const std::string& id, views::View* view,
                      gfx::Vector2d* delta) {
    auto it = before.find(id);
    if (it == before.end()) {
      return false;
    }
    gfx::Point now;
    views::View::ConvertPointToTarget(view, this, &now);
    *delta = it->second - now;
    return !delta->IsZero();
  };
  auto play_rows = [&](const std::vector<Row>& rows) {
    for (const Row& row : rows) {
      gfx::Vector2d delta;
      if (!delta_of(row.tab_id, row.view, &delta)) {
        continue;
      }
      if (row.tab_id == moved_id) {
        // The tab that moved goes on the spring.
        row.view->Settle(delta.y(), prefs);
      } else {
        // The rows around it close the gap on `base` and `standard`.
        row.view->SetDragOffset(delta.y());
        row.view->ShiftTo(0, prefs);
      }
    }
  };
  play_rows(pinned_rows_);
  play_rows(rows_);
  for (const Tile& tile : tiles_) {
    gfx::Vector2d delta;
    if (delta_of(tile.tab_id, tile.view, &delta)) {
      tile.view->PlayFlipFrom(delta, prefs);
    }
  }
}

void AerieSidebarView::SyncFavorites() {
  std::vector<std::string> wanted;
  if (favorites_list_) {
    for (const std::string& id : favorites_list_->rows()) {
      if (FindTab(id)) {
        wanted.push_back(id);
      }
    }
  }
  std::map<std::string, raw_ptr<AerieSidebarTileView>> existing;
  for (const Tile& tile : tiles_) {
    existing[tile.tab_id] = tile.view;
  }
  std::vector<Tile> next;
  for (const std::string& id : wanted) {
    auto it = existing.find(id);
    if (it != existing.end()) {
      next.push_back({id, it->second});
      existing.erase(it);
      continue;
    }
    AerieSidebarTileView::Callbacks callbacks;
    callbacks.select = base::BindRepeating(&AerieSidebarView::SelectTab,
                                           base::Unretained(this), id);
    callbacks.close = base::BindRepeating(&AerieSidebarView::CloseTab,
                                          base::Unretained(this), id);
    callbacks.key = base::BindRepeating(&AerieSidebarView::OnTileKey,
                                        base::Unretained(this), id);
    callbacks.focused = base::BindRepeating(&AerieSidebarView::OnTileFocused,
                                            base::Unretained(this));
    callbacks.begin_rename =
        base::BindRepeating(base::IgnoreResult(&AerieSidebarView::StartRename),
                            base::Unretained(this), id);
    callbacks.context_menu = base::BindRepeating(
        &AerieSidebarView::ShowRowContextMenu, base::Unretained(this), id);
    auto* view = favorites_grid_->AddChildView(
        std::make_unique<AerieSidebarTileView>(std::move(callbacks), palette_));
    next.push_back({id, view});
  }
  int refocus_index = -1;
  for (auto& [id, view] : existing) {
    if (view->HasFocus()) {
      refocus_index = IndexOfTile(id);
    }
    favorites_grid_->RemoveChildViewT(view.get());
  }
  tiles_ = std::move(next);
  for (size_t i = 0; i < tiles_.size(); ++i) {
    favorites_grid_->ReorderChildView(tiles_[i].view, i);
  }
  favorites_grid_->SetVisible(!tiles_.empty());
  favorites_grid_->InvalidateLayout();
  for (const Tile& tile : tiles_) {
    UpdateTile(tile);
  }
  UpdateTileTabStop();
  // The popover of a tile that is gone (the tab closed, or it left Favorites)
  // closes and keeps nothing.
  if (rename_popover_) {
    bool has_tile = false;
    for (const Tile& tile : tiles_) {
      has_tile = has_tile || tile.tab_id == rename_popover_tab_id_;
    }
    if (!has_tile) {
      rename_popover_->CancelRename();
    }
  }
  if (refocus_index >= 0 && !tiles_.empty()) {
    FocusTile(std::min(refocus_index, static_cast<int>(tiles_.size()) - 1));
  }
}

void AerieSidebarView::SyncPinned(
    const std::vector<aerie_tab_model::TreeRow>& wanted) {
  const int old_count = static_cast<int>(pinned_rows_.size());
  Reconciled result =
      ReconcileRows(pinned_rows_, wanted, pinned_list_, /*today=*/false);
  int refocus_index = -1;
  for (Row& row : result.gone) {
    if (KeepAsCollapsing(row)) {
      continue;
    }
    if (row.view->HasFocus() || row.tab_id == pinned_refocus_after_close_id_) {
      refocus_index = aerie_sidebar::HandleListKey(
                          old_count, result.old_index[row.tab_id],
                          aerie_sidebar::ListKey::kDelete)
                          .focus_index;
    }
    pinned_list_->RemoveChildViewT(row.view.get());
  }
  pinned_rows_ = std::move(result.next);
  pinned_tree_rows_ = wanted;
  OrderRows(pinned_list_, pinned_rows_);
  pinned_scroll_->SetVisible(
      !pinned_rows_.empty() ||
      std::any_of(collapsing_rows_.begin(), collapsing_rows_.end(),
                  [this](const Row& row) {
                    return row.view->parent() == pinned_list_;
                  }));
  pinned_list_->InvalidateLayout();
  for (const Row& row : pinned_rows_) {
    UpdateRow(row);
  }
  UpdatePinnedTabStop();
  if (refocus_index >= 0) {
    pinned_refocus_after_close_id_.clear();
    if (!pinned_rows_.empty()) {
      FocusPinnedRow(
          std::min(refocus_index, static_cast<int>(pinned_rows_.size()) - 1));
    } else {
      FocusTabStop();
    }
  }
}

bool AerieSidebarView::OnRowDragBegin(const std::string& tab_id) {
  if (drag_.active || rows_.size() < 2 || !closing_rows_.empty() ||
      !collapsing_rows_.empty()) {
    return false;
  }
  // Until issue #182 a drag only reorders a flat list: with folders in Today
  // the slots are not a plain order.
  for (const aerie_tab_model::TreeRow& row : today_tree_rows_) {
    if (row.is_folder || row.level > 0) {
      return false;
    }
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
  SyncRows(WantedRows(aerie_tab_model::Section::kToday));
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

void AerieSidebarView::OnTreeRowKey(bool today,
                                    const std::string& id,
                                    aerie_sidebar::ListKey key) {
  const std::vector<Row>& rows = today ? rows_ : pinned_rows_;
  const std::vector<aerie_tab_model::TreeRow>& tree =
      today ? today_tree_rows_ : pinned_tree_rows_;
  int focused = -1;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].tab_id == id) {
      focused = static_cast<int>(i);
    }
  }
  if (focused < 0 || tree.size() != rows.size()) {
    return;
  }
  const aerie_sidebar::TreeKeyResult result =
      aerie_sidebar::HandleTreeKey(tree, focused, key);
  if (result.close_focused) {
    // Focus moves when the row has gone (the sync), to the row that takes its
    // place.
    (today ? refocus_after_close_id_ : pinned_refocus_after_close_id_) = id;
    CloseTab(id);
    return;
  }
  if (!result.toggle_folder_id.empty()) {
    SetFolderExpanded(result.toggle_folder_id, result.expand);
    return;
  }
  if (today) {
    FocusRow(result.focus_index);
  } else {
    FocusPinnedRow(result.focus_index);
  }
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
  if (favorites_grid_->GetVisible()) {
    panes->push_back(favorites_grid_);
  }
  if (pinned_scroll_->GetVisible()) {
    panes->push_back(pinned_list_);
  }
  panes->push_back(list_);
  if (adopted_toolbar_) {
    panes->push_back(adopted_toolbar_);
  }
}

void AerieSidebarView::UpdateRows() {
  const std::map<std::string, tabs::TabInterface*> lookup = BuildTabLookup();
  base::AutoReset<raw_ptr<const std::map<std::string, tabs::TabInterface*>>>
      use_lookup(&tab_lookup_, &lookup);
  for (const Row& row : rows_) {
    UpdateRow(row);
  }
  for (const Row& row : pinned_rows_) {
    UpdateRow(row);
  }
  for (const Tile& tile : tiles_) {
    UpdateTile(tile);
  }
}

std::string AerieSidebarView::ActiveRecordId() const {
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser_->GetProfile());
  if (!service || !service->adapter()) {
    return std::string();
  }
  tabs::TabInterface* active = browser_->GetTabStripModel()->GetActiveTab();
  return active ? service->adapter()->RecordIdForTab(active) : std::string();
}

void AerieSidebarView::UpdateFolderRow(const Row& row) {
  const aerie_tab_model::FolderRecord* folder =
      model_ ? model_->GetFolder(row.tab_id) : nullptr;
  if (!folder) {
    return;
  }
  const std::u16string name = base::UTF8ToUTF16(folder->name);
  const std::u16string items = l10n_util::GetPluralStringFUTF16(
      IDS_AERIE_SIDEBAR_FOLDER_ITEMS, row.item_count);
  const std::string active = ActiveRecordId();
  const bool holds_selected =
      !active.empty() &&
      aerie_tab_model::FolderContainsTab(*model_, folder->id, active);
  const bool auto_filing = aerie_tab_model::IsAutoFiling(*folder);
  // An auto-filing folder says so in its name: "Trips, auto-filing folder, ...".
  std::u16string accessible_name = l10n_util::GetStringFUTF16(
      auto_filing ? (folder->expanded
                         ? IDS_AERIE_SIDEBAR_AUTO_FOLDER_NAME_EXPANDED
                         : IDS_AERIE_SIDEBAR_AUTO_FOLDER_NAME_COLLAPSED)
                  : (folder->expanded
                         ? IDS_AERIE_SIDEBAR_FOLDER_NAME_EXPANDED
                         : IDS_AERIE_SIDEBAR_FOLDER_NAME_COLLAPSED),
      name, items);
  if (holds_selected && !folder->expanded) {
    accessible_name = l10n_util::GetStringFUTF16(
        IDS_AERIE_SIDEBAR_FOLDER_HAS_SELECTED, accessible_name);
  }
  row.view->SetFolderState(
      name, accessible_name, folder->expanded, holds_selected,
      CanAnimateRows() ? browser_->GetProfile()->GetPrefs() : nullptr);
  row.view->SetAutoFiling(
      auto_filing, l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FOLDER_RULES_BUTTON));
}

void AerieSidebarView::UpdateRow(const Row& row) {
  if (row.is_folder) {
    UpdateFolderRow(row);
    return;
  }
  tabs::TabInterface* tab = FindTab(row.tab_id);
  if (!tab) {
    return;
  }
  const aerie_tab_model::TabRecord* record =
      model_ ? model_->GetTab(row.tab_id) : nullptr;
  UpdateRowTitle(row, tab, record);
  row.view->SetFavicon(FaviconFor(tab, palette_.text_muted));
  row.view->SetSelected(tab->IsActivated());
  row.view->SetSleeping(record && record->sleeping,
                        browser_->GetProfile()->GetPrefs());
  // A pinned row names what its close button does and offers the way back to
  // the pinned page (issue #180).
  auto* controller = PinnedController();
  const bool pinned =
      record && record->section == aerie_tab_model::Section::kPinned;
  if (pinned && controller) {
    row.view->SetCloseTooltip(l10n_util::GetStringUTF16(
        CloseTooltipMessageId(controller->ActionFor(tab))));
    row.view->SetBackToPinnedVisible(controller->IsAwayFromPinnedPage(tab));
  } else {
    row.view->SetCloseTooltip(
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));
    row.view->SetBackToPinnedVisible(false);
  }
}

void AerieSidebarView::UpdateRowTitle(
    const Row& row,
    tabs::TabInterface* tab,
    const aerie_tab_model::TabRecord* record) {
  const ShownTitle shown = ShownTitleFor(tab, record);
  row.view->SetTitle(shown.text);
  row.view->SetRenamed(shown.renamed, shown.tooltip,
                       browser_->GetProfile()->GetPrefs());
}

void AerieSidebarView::UpdateTileTitle(
    const Tile& tile,
    tabs::TabInterface* tab,
    const aerie_tab_model::TabRecord* record) {
  const ShownTitle shown = ShownTitleFor(tab, record);
  tile.view->SetTitle(shown.text);
  tile.view->SetRenamed(shown.renamed, shown.tooltip);
}

void AerieSidebarView::UpdateTile(const Tile& tile) {
  tabs::TabInterface* tab = FindTab(tile.tab_id);
  if (!tab) {
    return;
  }
  const aerie_tab_model::TabRecord* record =
      model_ ? model_->GetTab(tile.tab_id) : nullptr;
  UpdateTileTitle(tile, tab, record);
  tile.view->SetFavicon(FaviconFor(tab, palette_.text_muted));
  tile.view->SetSelected(tab->IsActivated());
  tile.view->SetSleeping(record && record->sleeping,
                         browser_->GetProfile()->GetPrefs());
}

void AerieSidebarView::OnTabAdded(const aerie_tab_model::TabRecord&) {
  UpdateSleepingSummary();
}

void AerieSidebarView::OnTabMoved(const aerie_tab_model::TabRecord& tab,
                                  const aerie_tab_model::Placement& from) {
  // A change of section plays the FLIP motion for this tab (issue #180).
  if (from.section != tab.section) {
    flip_pending_ = true;
    flip_moved_id_ = tab.id;
    // The positions before the change are the ones on screen now.
    flip_before_ = CapturePositions();
  }
  ScheduleSync();
}

void AerieSidebarView::OnTabChanged(const aerie_tab_model::TabRecord& record) {
  // The Tab Model reports every change of a record, title and favicon churn
  // included; `sleeping` is the only field this view needs. The title and
  // favicon reach the rows from OnTabChangedAt. So only the sleeping state of
  // this one row is updated, and the Today summary is computed again only when
  // that state changes.
  // A manual name set or cleared is seen here (issue #145).
  for (const Tile& tile : tiles_) {
    if (tile.tab_id == record.id) {
      if (tabs::TabInterface* tab = FindTab(record.id)) {
        UpdateTileTitle(tile, tab, &record);
      }
      if (tile.view->sleeping() != record.sleeping) {
        tile.view->SetSleeping(record.sleeping,
                               browser_->GetProfile()->GetPrefs());
        UpdateSleepingSummary();
      }
      return;
    }
  }
  for (const Row& row : pinned_rows_) {
    if (row.tab_id == record.id) {
      if (tabs::TabInterface* tab = FindTab(record.id)) {
        UpdateRowTitle(row, tab, &record);
      }
      if (row.view->sleeping() != record.sleeping) {
        row.view->SetSleeping(record.sleeping,
                              browser_->GetProfile()->GetPrefs());
        UpdateSleepingSummary();
      }
      return;
    }
  }
  for (const Row& row : rows_) {
    if (row.tab_id != record.id) {
      continue;
    }
    if (tabs::TabInterface* tab = FindTab(record.id)) {
      UpdateRowTitle(row, tab, &record);
    }
    if (row.view->sleeping() != record.sleeping) {
      row.view->SetSleeping(record.sleeping,
                            browser_->GetProfile()->GetPrefs());
      UpdateSleepingSummary();
    }
    return;
  }
  // No row yet: the row reads the field when it is made; the summary counts
  // records.
  UpdateSleepingSummary();
}

void AerieSidebarView::OnTabRemoved(const aerie_tab_model::TabRecord&) {
  UpdateSleepingSummary();
}

void AerieSidebarView::OnFolderAdded(const aerie_tab_model::FolderRecord&) {
  ScheduleSync();
}

void AerieSidebarView::OnFolderChanged(const aerie_tab_model::FolderRecord&) {
  // A name, a state or a count: the sync updates the folder rows.
  ScheduleSync();
}

void AerieSidebarView::OnFolderMoved(const aerie_tab_model::FolderRecord&,
                                     const aerie_tab_model::Placement&) {
  ScheduleSync();
}

void AerieSidebarView::OnFolderRemoved(const aerie_tab_model::FolderRecord&) {
  ScheduleSync();
}

void AerieSidebarView::UpdateSleepingSummary() {
  const int count = model_ ? aerie_tab_model::CountSleepingTabs(*model_) : 0;
  if (count == 0) {
    sleeping_summary_->SetVisible(false);
    sleeping_summary_->SetText(std::u16string());
    return;
  }
  int megabytes = 0;
  if (auto* controller =
          aerie_tab_model::SleepControllerFactory::GetForProfileIfExists(
              browser_->GetProfile())) {
    megabytes = aerie_tab_model::RoundMemoryEstimateMb(
        controller->sleeping_memory_estimate_bytes());
  }
  sleeping_summary_->SetText(
      megabytes > 0
          ? l10n_util::GetStringFUTF16(
                IDS_AERIE_SIDEBAR_SLEEPING_COUNT_AND_MEMORY,
                base::NumberToString16(count),
                base::NumberToString16(megabytes))
          : l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_SLEEPING_COUNT,
                                       base::NumberToString16(count)));
  sleeping_summary_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEPING_TOOLTIP));
  sleeping_summary_->SetVisible(true);
}

// --- Folders (issue #181) ---

bool AerieSidebarView::IsInsideFolder(const std::string& folder_id,
                                      const std::string& id) const {
  if (!model_) {
    return false;
  }
  if (model_->GetTab(id)) {
    return aerie_tab_model::FolderContainsTab(*model_, folder_id, id);
  }
  const aerie_tab_model::FolderRecord* walk = model_->GetFolder(id);
  // A folder is not inside itself.
  walk = walk && !walk->parent.empty() ? model_->GetFolder(walk->parent)
                                       : nullptr;
  for (int steps = 0; walk && steps <= aerie_tab_model::kWalkBound; ++steps) {
    if (walk->id == folder_id) {
      return true;
    }
    walk = walk->parent.empty() ? nullptr : model_->GetFolder(walk->parent);
  }
  return false;
}

std::optional<AerieSidebarView::RowPlace> AerieSidebarView::FindRow(
    const std::string& id) const {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].tab_id == id) {
      return RowPlace{true, i};
    }
  }
  for (size_t i = 0; i < pinned_rows_.size(); ++i) {
    if (pinned_rows_[i].tab_id == id) {
      return RowPlace{false, i};
    }
  }
  return std::nullopt;
}

AerieSidebarRowView* AerieSidebarView::RowForId(const std::string& id) const {
  const std::optional<RowPlace> place = FindRow(id);
  if (!place) {
    return nullptr;
  }
  return (place->today ? rows_ : pinned_rows_)[place->index].view;
}

const std::vector<aerie_tab_model::TreeRow>& AerieSidebarView::tree_rows(
    aerie_tab_model::Section section) const {
  return section == aerie_tab_model::Section::kPinned ? pinned_tree_rows_
                                                      : today_tree_rows_;
}

void AerieSidebarView::FocusRowById(const std::string& id) {
  if (const std::optional<RowPlace> place = FindRow(id)) {
    const int index = static_cast<int>(place->index);
    place->today ? FocusRow(index) : FocusPinnedRow(index);
  }
}

void AerieSidebarView::ToggleFolder(const std::string& folder_id) {
  const aerie_tab_model::FolderRecord* folder =
      model_ ? model_->GetFolder(folder_id) : nullptr;
  if (folder) {
    SetFolderExpanded(folder_id, !folder->expanded);
  }
}

void AerieSidebarView::SetFolderExpanded(const std::string& folder_id,
                                         bool expanded) {
  const aerie_tab_model::FolderRecord* folder =
      model_ ? model_->GetFolder(folder_id) : nullptr;
  if (!folder || folder->expanded == expanded) {
    return;
  }
  // Keyboard focus on a row that is about to be hidden goes to the folder row.
  refocus_folder_id_.clear();
  if (!expanded) {
    for (const std::vector<Row>* rows : {&rows_, &pinned_rows_}) {
      for (const Row& row : *rows) {
        if (row.view->HasFocus() && IsInsideFolder(folder_id, row.tab_id)) {
          refocus_folder_id_ = folder_id;
        }
      }
    }
  }
  toggle_ = {true, folder_id, expanded};
  // The sync runs now, with the toggle known, instead of in a task: a second
  // sync after it would end the motion.
  const bool was_pending = sync_pending_;
  sync_pending_ = true;
  aerie_tab_model::SetFolderExpanded(*model_, folder_id, expanded);
  sync_pending_ = was_pending;
  SyncAll();
  toggle_ = PendingToggle();
  if (!refocus_folder_id_.empty()) {
    FocusRowById(refocus_folder_id_);
    refocus_folder_id_.clear();
  }
}

void AerieSidebarView::StartToggleGroup() {
  const PendingToggle toggle = toggle_;
  std::vector<raw_ptr<AerieSidebarRowView>> revealed =
      std::move(toggle_revealed_);
  std::vector<raw_ptr<AerieSidebarRowView>> collapsing =
      std::move(toggle_collapsing_);
  toggle_revealed_.clear();
  toggle_collapsing_.clear();
  if (!toggle.active) {
    return;
  }
  if (group_ && group_->running()) {
    if (group_->folder_id() == toggle.folder_id) {
      group_->Retarget(toggle.expand);
      return;
    }
    group_->Finish();
  }
  std::vector<raw_ptr<AerieSidebarRowView>> rows =
      toggle.expand ? std::move(revealed) : std::move(collapsing);
  // A row can go between the sync that found it and here (ending another
  // folder's motion removes the rows that were collapsing). Only rows that are
  // still the sidebar's own are animated; this compares pointers and never
  // reads a row that may be gone.
  std::erase_if(rows, [this](const raw_ptr<AerieSidebarRowView>& row) {
    return !IsLiveRow(row.get());
  });
  if (rows.empty() || !CanAnimateRows()) {
    RemoveCollapsedRows();
    return;
  }
  views::View* list = rows.front()->parent();
  std::sort(rows.begin(), rows.end(),
            [list](const raw_ptr<AerieSidebarRowView>& a,
                   const raw_ptr<AerieSidebarRowView>& b) {
              return list->GetIndexOf(a.get()).value_or(0) <
                     list->GetIndexOf(b.get()).value_or(0);
            });
  group_ = std::make_unique<AerieSidebarFolderGroup>(
      toggle.folder_id, std::move(rows), list,
      browser_->GetProfile()->GetPrefs(),
      base::BindOnce(&AerieSidebarView::RemoveCollapsedRows,
                     base::Unretained(this)));
  group_->Start(toggle.expand);
}

bool AerieSidebarView::IsLiveRow(const AerieSidebarRowView* row) const {
  for (const std::vector<Row>* rows : {&rows_, &pinned_rows_, &collapsing_rows_}) {
    for (const Row& entry : *rows) {
      if (entry.view == row) {
        return true;
      }
    }
  }
  return false;
}

void AerieSidebarView::RemoveCollapsedRows() {
  std::vector<Row> rows = std::move(collapsing_rows_);
  collapsing_rows_.clear();
  toggle_collapsing_.clear();
  for (const Row& row : rows) {
    if (views::View* parent = row.view->parent()) {
      parent->RemoveChildViewT(row.view.get());
      parent->InvalidateLayout();
    }
  }
  if (!pinned_scroll_ || !pinned_list_) {
    return;
  }
  pinned_scroll_->SetVisible(!pinned_rows_.empty());
}

std::string AerieSidebarView::CreateFolderIn(
    aerie_tab_model::Section section,
    const std::string& parent_folder_id) {
  if (!model_ || !aerie_tab_model::CanCreateFolderIn(*model_, parent_folder_id)) {
    return std::string();
  }
  // A folder made inside a shut folder shows: open the parent first.
  if (!parent_folder_id.empty()) {
    aerie_tab_model::SetFolderExpanded(*model_, parent_folder_id, true);
  }
  const std::string id = aerie_tab_model::CreateFolder(
      *model_, section, parent_folder_id,
      l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_NEW_FOLDER));
  if (!id.empty()) {
    RenameNewFolderSoon(id);
  }
  return id;
}

void AerieSidebarView::RenameNewFolderSoon(const std::string& folder_id) {
  // After the sync that makes the row, and after a menu that is closing has
  // given focus back.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<AerieSidebarView> self, std::string id) {
            if (self) {
              self->StartRename(id);
            }
          },
          weak_factory_.GetWeakPtr(), folder_id));
}

bool AerieSidebarView::RequestDeleteFolder(const std::string& folder_id) {
  if (!model_ || !model_->GetFolder(folder_id)) {
    return false;
  }
  if (aerie_tab_model::FolderIsEmpty(*model_, folder_id)) {
    DeleteFolderNow(folder_id);
    return false;
  }
  int items = 0;
  for (const auto& [id, tab] : model_->tabs()) {
    items += tab.parent == folder_id ? 1 : 0;
  }
  for (const auto& [id, folder] : model_->folders()) {
    items += folder.parent == folder_id ? 1 : 0;
  }
  // Without a window there is nothing to show the question in: nothing is
  // asked and the flag stays clear.
  if (!GetWidget()) {
    return false;
  }
  auto closed = base::BindOnce(
      [](base::WeakPtr<AerieSidebarView> self) {
        if (self) {
          self->delete_question_open_ = false;
        }
      },
      weak_factory_.GetWeakPtr());
  auto accepted = base::BindOnce(
      [](base::WeakPtr<AerieSidebarView> self, std::string id) {
        if (self) {
          self->DeleteFolderNow(id);
        }
      },
      weak_factory_.GetWeakPtr(), folder_id);
  ui::DialogModel::Builder builder;
  builder
      .SetTitle(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FOLDER_DELETE_TITLE))
      .AddParagraph(ui::DialogModelLabel(l10n_util::GetStringFUTF16(
          IDS_AERIE_SIDEBAR_FOLDER_DELETE_BODY,
          l10n_util::GetPluralStringFUTF16(IDS_AERIE_SIDEBAR_FOLDER_ITEMS,
                                           items))))
      .AddOkButton(
          std::move(accepted),
          ui::DialogModel::Button::Params().SetLabel(
              l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_DELETE_FOLDER)))
      .AddCancelButton(base::DoNothing())
      .SetDialogDestroyingCallback(std::move(closed));
  views::Widget* dialog = constrained_window::ShowBrowserModal(
      builder.Build(), GetWidget()->GetNativeWindow());
  // The flag says a question is on screen, so it is set once it is.
  delete_question_open_ = dialog != nullptr;
  return delete_question_open_;
}

void AerieSidebarView::DeleteFolderNow(const std::string& folder_id) {
  if (model_) {
    aerie_tab_model::DeleteFolder(*model_, folder_id);
  }
}

void AerieSidebarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  const bool pinned = source == pinned_list_ || source == pinned_scroll_;
  ShowSectionContextMenu(pinned ? aerie_tab_model::Section::kPinned
                                : aerie_tab_model::Section::kToday,
                         point, source_type);
}

void AerieSidebarView::ShowSectionContextMenu(
    aerie_tab_model::Section section,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!model_ || !GetWidget()) {
    return;
  }
  AerieSidebarContextMenu::Options options;
  options.target = AerieSidebarContextMenu::Target::kSection;
  options.section = section;
  options.show_new_folder = true;
  options.new_folder_enabled = true;
  context_menu_ = std::make_unique<AerieSidebarContextMenu>(
      std::move(options),
      base::BindRepeating(&AerieSidebarView::OnSectionContextCommand,
                          weak_factory_.GetWeakPtr(), section));
  context_menu_->Run(GetWidget(), screen_point, source_type,
                     browser_->GetProfile()->GetPrefs());
}

void AerieSidebarView::OnSectionContextCommand(
    aerie_tab_model::Section section,
    int command) {
  if (command == AerieSidebarContextMenu::kCommandNewFolder) {
    CreateFolderIn(section, std::string());
  }
}

void AerieSidebarView::ShowFolderContextMenu(
    const std::string& folder_id,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (!model_ || !GetWidget() || !model_->GetFolder(folder_id)) {
    return;
  }
  AerieSidebarContextMenu::Options options;
  options.target = AerieSidebarContextMenu::Target::kFolder;
  options.section = model_->GetFolder(folder_id)->section;
  options.show_new_folder = true;
  options.new_folder_enabled =
      aerie_tab_model::CanCreateFolderIn(*model_, folder_id);
  if (!options.new_folder_enabled) {
    options.new_folder_tooltip = TooDeepTooltip();
  }
  context_menu_ = std::make_unique<AerieSidebarContextMenu>(
      std::move(options),
      base::BindRepeating(&AerieSidebarView::OnFolderContextCommand,
                          weak_factory_.GetWeakPtr(), folder_id));
  context_menu_->Run(GetWidget(), screen_point, source_type,
                     browser_->GetProfile()->GetPrefs());
}

void AerieSidebarView::OnFolderContextCommand(const std::string& folder_id,
                                              int command) {
  if (!model_ || !model_->GetFolder(folder_id)) {
    return;
  }
  switch (command) {
    case AerieSidebarContextMenu::kCommandNewFolder:
      CreateFolderIn(model_->GetFolder(folder_id)->section, folder_id);
      break;
    case AerieSidebarContextMenu::kCommandRenameFolder:
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<AerieSidebarView> self, std::string id) {
                if (self) {
                  self->StartRename(id);
                }
              },
              weak_factory_.GetWeakPtr(), folder_id));
      break;
    case AerieSidebarContextMenu::kCommandEditFolder:
      // Open the sheet after this menu command returns, so that the menu has
      // closed and given up its grab: the sheet anchors on the row and takes
      // focus, and neither works while the menu still owns them.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<AerieSidebarView> self, std::string id) {
                if (self) {
                  self->OpenFolderSheet(id);
                }
              },
              weak_factory_.GetWeakPtr(), folder_id));
      break;
    case AerieSidebarContextMenu::kCommandDeleteFolder:
      RequestDeleteFolder(folder_id);
      break;
  }
}

aerie_tab_model::PinnedTabsController* AerieSidebarView::PinnedController()
    const {
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser_->GetProfile());
  return service ? service->pinned_tabs() : nullptr;
}

void AerieSidebarView::ShowRowContextMenu(
    const std::string& tab_id,
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  if (model_ && model_->GetFolder(tab_id)) {
    ShowFolderContextMenu(tab_id, screen_point, source_type);
    return;
  }
  tabs::TabInterface* tab = FindTab(tab_id);
  auto* controller = aerie_tab_model::SleepControllerFactory::GetForProfile(
      browser_->GetProfile());
  const aerie_tab_model::TabRecord* record =
      model_ ? model_->GetTab(tab_id) : nullptr;
  if (!tab || !controller || !record || !GetWidget()) {
    return;
  }
  using Eligibility = aerie_tab_model::SleepController::Eligibility;
  const Eligibility eligibility = controller->GetEligibility(tab);
  AerieSidebarContextMenu::Options options;
  options.section = record->section;
  if (auto* pinned = PinnedController()) {
    options.away_from_pinned = pinned->IsAwayFromPinnedPage(tab);
  }
  options.has_manual_name = record->manual_name.has_value();
  options.sleep.enabled = eligibility.state == Eligibility::State::kCanSleep;
  if (!options.sleep.enabled) {
    options.sleep.tooltip =
        l10n_util::GetStringUTF16(eligibility.tooltip_message_id);
  }
  if (record->section != aerie_tab_model::Section::kFavorite) {
    const aerie_tab_model::Section section = record->section;
    const bool in_folder = !record->parent.empty();
    options.show_new_folder = true;
    // Both items make a folder where the tab is, so one test serves both.
    const bool can_make_folder =
        aerie_tab_model::CanCreateFolderIn(*model_, record->parent);
    options.new_folder_enabled = can_make_folder;
    options.show_move_to_new_folder = true;
    options.move_to_new_folder_enabled = can_make_folder;
    if (!can_make_folder) {
      options.new_folder_tooltip = TooDeepTooltip();
      options.move_to_new_folder_tooltip = TooDeepTooltip();
    }
    options.show_move_out = in_folder;
    options.show_stop_rule = record->filed_by.has_value();
    for (const aerie_tab_model::FolderChoice& choice :
         aerie_tab_model::FolderChoices(*model_, section)) {
      if (choice.id == record->parent) {
        continue;
      }
      AerieSidebarContextMenu::MoveTarget target;
      target.folder_id = choice.id;
      target.label = std::u16string(choice.level > 1 ? 2 * (choice.level - 1) : 0,
                                    u' ') +
                     base::UTF8ToUTF16(choice.name);
      options.move_targets.push_back(std::move(target));
    }
  }
  context_menu_ = std::make_unique<AerieSidebarContextMenu>(
      std::move(options),
      base::BindRepeating(&AerieSidebarView::OnContextCommand,
                          weak_factory_.GetWeakPtr(), tab_id));
  context_menu_->Run(GetWidget(), screen_point, source_type,
                     browser_->GetProfile()->GetPrefs());
}

void AerieSidebarView::OnContextCommand(const std::string& tab_id,
                                        int command) {
  tabs::TabInterface* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  auto* pinned = PinnedController();
  switch (command) {
    case AerieSidebarContextMenu::kCommandSleepTab:
      if (auto* sleep = aerie_tab_model::SleepControllerFactory::GetForProfile(
              browser_->GetProfile())) {
        sleep->Sleep(tab);
      }
      break;
    case AerieSidebarContextMenu::kCommandBackToPinned:
      GoBackToPinnedPage(tab_id);
      break;
    case AerieSidebarContextMenu::kCommandPin:
      if (pinned) {
        pinned->Pin(tab);
      }
      break;
    case AerieSidebarContextMenu::kCommandUnpin:
      if (pinned) {
        pinned->Unpin(tab);
      }
      break;
    case AerieSidebarContextMenu::kCommandAddToFavorites:
      if (pinned && pinned->AddToFavorites(tab) ==
                        aerie_tab_model::FavoriteResult::kFull) {
        ShowToast(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FAVORITES_FULL));
      }
      break;
    case AerieSidebarContextMenu::kCommandRemoveFromFavorites:
      if (pinned) {
        pinned->RemoveFromFavorites(tab);
      }
      break;
    case AerieSidebarContextMenu::kCommandClose:
      CloseTab(tab_id);
      break;
    case AerieSidebarContextMenu::kCommandRename:
      // The menu is still closing and giving focus back: the field takes focus
      // after that.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<AerieSidebarView> self, std::string id) {
                if (self) {
                  self->StartRename(id);
                }
              },
              weak_factory_.GetWeakPtr(), tab_id));
      break;
    case AerieSidebarContextMenu::kCommandResetName:
      ResetName(tab_id);
      break;
    case AerieSidebarContextMenu::kCommandNewFolder: {
      const aerie_tab_model::TabRecord* record =
          model_ ? model_->GetTab(tab_id) : nullptr;
      if (record) {
        CreateFolderIn(record->section, record->parent);
      }
      break;
    }
    case AerieSidebarContextMenu::kCommandMoveToNewFolder:
      if (model_) {
        const std::string id = aerie_tab_model::MoveTabToNewFolder(
            *model_, tab_id,
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_NEW_FOLDER));
        if (!id.empty()) {
          RenameNewFolderSoon(id);
        }
      }
      break;
    case AerieSidebarContextMenu::kCommandStopRuleMatching:
      // The tab is an exception of its folder from now on, and it leaves the
      // folder (the user said the rule is wrong for it).
      if (model_ && aerie_tab_model::StopRuleMatchingTab(*model_, tab_id)) {
        aerie_tab_model::UndoFiling(*model_, tab_id);
      }
      break;
    case AerieSidebarContextMenu::kCommandMoveOutOfFolder:
      if (model_) {
        aerie_tab_model::MoveTabToFolder(*model_, tab_id, std::string());
      }
      break;
    default: {
      const std::string folder =
          context_menu_ ? context_menu_->FolderIdForCommand(command)
                        : std::string();
      if (model_ && !folder.empty()) {
        aerie_tab_model::MoveTabToFolder(*model_, tab_id, folder);
      }
    }
  }
}

bool AerieSidebarView::StartRename(const std::string& tab_id) {
  if (const aerie_tab_model::FolderRecord* folder =
          model_ ? model_->GetFolder(tab_id) : nullptr) {
    // A folder row renames in place with the field of a tab row.
    if (AerieSidebarRowView* row = RowForId(tab_id)) {
      row->BeginRename(base::UTF8ToUTF16(folder->name),
                       browser_->GetProfile()->GetPrefs());
      return row->renaming();
    }
    return false;
  }
  tabs::TabInterface* tab = FindTab(tab_id);
  const aerie_tab_model::TabRecord* record =
      model_ ? model_->GetTab(tab_id) : nullptr;
  if (!tab || !record) {
    return false;
  }
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  // The field opens with what the row shows: the name, or the page title.
  const std::u16string initial = ShownTitleFor(tab, record).text;
  if (AerieSidebarRowView* row = RowForId(tab_id)) {
    row->BeginRename(initial, prefs);
    return row->renaming();
  }
  for (const Tile& tile : tiles_) {
    if (tile.tab_id != tab_id) {
      continue;
    }
    // A tile has no title: the field is in a popover beside it.
    if (rename_popover_) {
      return true;
    }
    rename_popover_tab_id_ = tab_id;
    rename_popover_ = AerieSidebarRenamePopover::Show(
                          tile.view, initial, prefs,
                          base::BindOnce(&AerieSidebarView::OnTilePopoverDone,
                                         weak_factory_.GetWeakPtr(), tab_id))
                          ->GetWeakPtr();
    return true;
  }
  return false;
}

void AerieSidebarView::OnTilePopoverDone(const std::string& tab_id,
                                         std::optional<std::u16string> text,
                                         bool refocus_anchor) {
  rename_popover_.reset();
  rename_popover_tab_id_.clear();
  if (text) {
    CommitRename(tab_id, *text);
  }
  if (refocus_anchor) {
    for (const Tile& tile : tiles_) {
      if (tile.tab_id == tab_id) {
        tile.view->RequestFocus();
      }
    }
  }
}

void AerieSidebarView::CommitRename(const std::string& tab_id,
                                    const std::u16string& text) {
  if (model_ && model_->GetFolder(tab_id)) {
    // An empty name keeps the old one.
    aerie_tab_model::RenameFolder(*model_, tab_id, base::UTF16ToUTF8(text));
    return;
  }
  tabs::TabInterface* tab = FindTab(tab_id);
  if (!tab || !model_) {
    return;
  }
  // Only the Tab Model changes: the page, its history and the window title keep
  // the real title.
  aerie_tab_model::ApplyManualName(*model_, tab_id, base::UTF16ToUTF8(text),
                                   base::UTF16ToUTF8(tab->GetTitle()));
}

void AerieSidebarView::ResetName(const std::string& tab_id) {
  CommitRename(tab_id, std::u16string());
}

ui::Layer* AerieSidebarView::toast_layer() const {
  return toast_card_->layer();
}

bool AerieSidebarView::toast_visible() const {
  return toast_card_->GetVisible();
}

void AerieSidebarView::ShowToast(const std::u16string& text, bool has_actions) {
  toast_motion_.Abort();
  toast_timer_.Stop();
  toast_->SetText(text);
  toast_actions_->SetVisible(has_actions);
  toast_card_->SetVisible(true);
  // A polite announcement: the label has the status role, and its text change
  // is what a screen reader reads.
  toast_->NotifyAccessibilityEventDeprecated(ax::mojom::Event::kAlert, true);
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  const aerie_sidebar::ToastMotion motion = aerie_sidebar::ToastShowMotion(
      aerie::motion::ShouldReduceMotion(prefs), has_actions);
  if (!toast_card_->layer()) {
    toast_card_->SetPaintToLayer();
    toast_card_->layer()->SetFillsBoundsOpaquely(false);
  }
  ui::Layer* layer = toast_card_->layer();
  layer->SetOpacity(motion.from_opacity);
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  if (motion.animate_rise) {
    layer->SetTransform(gfx::Transform::MakeTranslation(0, motion.rise_px));
    target.transform = gfx::Transform();
  } else {
    layer->SetTransform(gfx::Transform());
  }
  toast_motion_ = aerie::motion::AnimateLayer(layer, target, motion.duration,
                                              motion.easing, prefs);
  toast_timer_.Start(FROM_HERE, base::Milliseconds(motion.hold_ms),
                     base::BindOnce(&AerieSidebarView::HideToast,
                                    base::Unretained(this)));
}

void AerieSidebarView::HideToast() {
  toast_motion_.Abort();
  toast_timer_.Stop();
  if (!toast_card_->GetVisible() || !toast_card_->layer()) {
    return;
  }
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  const aerie_sidebar::ToastMotion motion = aerie_sidebar::ToastHideMotion(
      aerie::motion::ShouldReduceMotion(prefs), toast_actions_->GetVisible());
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  toast_motion_ = aerie::motion::AnimateLayer(toast_card_->layer(), target,
                                              motion.duration, motion.easing,
                                              prefs);
  // The card goes when the fade has ended.
  toast_timer_.Start(
      FROM_HERE, base::Milliseconds(motion.duration_ms),
      base::BindOnce(&AerieSidebarView::FinishToastHide,
                     base::Unretained(this)));
}

void AerieSidebarView::FinishToastHide() {
  toast_motion_.Abort();
  toast_card_->SetVisible(false);
  toast_actions_->SetVisible(false);
  toast_->SetText(std::u16string());
  last_filed_ = aerie_tab_model::AutoFileController::Filed();
  if (toast_card_->layer()) {
    toast_card_->DestroyLayer();
  }
}

void AerieSidebarView::OnAutoFileControllerDestroyed() {
  // The service shuts down (and drops the controller) before this window is
  // gone in some orders, such as a profile that is deleted: let go of it.
  auto_file_ = nullptr;
}

void AerieSidebarView::OnTabFiled(
    const aerie_tab_model::AutoFileController::Filed& filed) {
  // Only the window that holds the tab shows the notice and the pulse.
  if (!FindTab(filed.tab_id) || !model_) {
    return;
  }
  const aerie_tab_model::FolderRecord* folder =
      model_->GetFolder(filed.folder_id);
  if (!folder) {
    return;
  }
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  if (AerieSidebarRowView* row = RowForId(filed.folder_id)) {
    row->PlayFilingPulse(prefs);
  }
  last_filed_ = filed;
  ShowToast(l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_FILED_INTO,
                                       base::UTF8ToUTF16(folder->name)),
            /*has_actions=*/true);
}

void AerieSidebarView::UndoFiling() {
  if (model_ && !last_filed_.tab_id.empty()) {
    aerie_tab_model::UndoFiling(*model_, last_filed_.tab_id);
  }
  HideToast();
}

void AerieSidebarView::DontFileThisSite() {
  if (model_ && !last_filed_.folder_id.empty() && !last_filed_.host.empty()) {
    aerie_tab_model::AddExcludedHost(*model_, last_filed_.folder_id,
                                     last_filed_.host);
  }
  HideToast();
}

void AerieSidebarView::OpenFolderSheet(const std::string& folder_id) {
  if (!model_ || !GetWidget() || !model_->GetFolder(folder_id)) {
    return;
  }
  if (folder_sheet_) {
    const bool same = folder_sheet_->folder_id() == folder_id;
    folder_sheet_->Dismiss(/*refocus=*/false);
    if (same) {
      return;
    }
  }
  AerieSidebarRowView* row = RowForId(folder_id);
  if (!row) {
    return;
  }
  AerieSidebarFolderSheet::Params params;
  params.anchor = row;
  params.model = model_;
  params.prefs = browser_->GetProfile()->GetPrefs();
  params.folder_id = folder_id;
  params.palette = palette_;
  params.sidebar_on_right = OuterEdgeSign() > 0;
  params.closed = base::BindOnce(
      [](base::WeakPtr<AerieSidebarView> self, std::string id, bool refocus) {
        if (!self) {
          return;
        }
        // The window is closing: the sheet is gone as far as the sidebar goes,
        // whenever the window object itself is destroyed.
        self->folder_sheet_.reset();
        if (refocus) {
          self->FocusRowById(id);
        }
      },
      weak_factory_.GetWeakPtr(), folder_id);
  if (AerieSidebarFolderSheet* sheet =
          AerieSidebarFolderSheet::Show(std::move(params))) {
    folder_sheet_ = sheet->GetWeakPtr();
  }
}

void AerieSidebarView::GoBackToPinnedPage(const std::string& tab_id) {
  if (tabs::TabInterface* tab = FindTab(tab_id)) {
    if (auto* pinned = PinnedController()) {
      pinned->GoBackToPinnedPage(tab);
    }
  }
}

AerieSidebarRowView* AerieSidebarView::pinned_row_at(size_t index) const {
  return pinned_rows_[index].view;
}

std::string AerieSidebarView::pinned_tab_id_at(size_t index) const {
  return pinned_rows_[index].tab_id;
}

AerieSidebarRowView* AerieSidebarView::pinned_tab_stop_row() const {
  for (const Row& row : pinned_rows_) {
    if (row.view->is_tab_stop()) {
      return row.view;
    }
  }
  return nullptr;
}

AerieSidebarTileView* AerieSidebarView::tile_at(size_t index) const {
  return tiles_[index].view;
}

std::string AerieSidebarView::favorite_tab_id_at(size_t index) const {
  return tiles_[index].tab_id;
}

AerieSidebarTileView* AerieSidebarView::tile_stop() const {
  for (const Tile& tile : tiles_) {
    if (tile.view->is_tab_stop()) {
      return tile.view;
    }
  }
  return nullptr;
}

int AerieSidebarView::IndexOfPinnedRow(const std::string& tab_id) const {
  for (size_t i = 0; i < pinned_rows_.size(); ++i) {
    if (pinned_rows_[i].tab_id == tab_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int AerieSidebarView::IndexOfTile(const std::string& tab_id) const {
  for (size_t i = 0; i < tiles_.size(); ++i) {
    if (tiles_[i].tab_id == tab_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void AerieSidebarView::OnPinnedRowFocused() {
  pinned_refocus_after_close_id_.clear();
  UpdatePinnedTabStop();
}

void AerieSidebarView::UpdatePinnedTabStop() {
  AerieSidebarRowView* stop = nullptr;
  for (const Row& row : pinned_rows_) {
    if (row.view->HasFocus()) {
      stop = row.view;
      break;
    }
  }
  if (!stop) {
    for (const Row& row : pinned_rows_) {
      if (row.view->selected()) {
        stop = row.view;
        break;
      }
    }
  }
  if (!stop && !pinned_rows_.empty()) {
    stop = pinned_rows_.front().view;
  }
  for (const Row& row : pinned_rows_) {
    row.view->SetTabStop(row.view == stop);
  }
}

void AerieSidebarView::FocusPinnedRow(int index) {
  if (index < 0 || index >= static_cast<int>(pinned_rows_.size())) {
    return;
  }
  AerieSidebarRowView* target = pinned_rows_[index].view;
  for (const Row& row : pinned_rows_) {
    row.view->SetTabStop(row.view == target);
  }
  target->RequestFocus();
  target->ScrollViewToVisible();
}

void AerieSidebarView::FocusPinnedTabStop() {
  if (pinned_rows_.empty()) {
    return;
  }
  UpdatePinnedTabStop();
  for (size_t i = 0; i < pinned_rows_.size(); ++i) {
    if (pinned_rows_[i].view->is_tab_stop()) {
      FocusPinnedRow(static_cast<int>(i));
      return;
    }
  }
}

void AerieSidebarView::OnTileKey(const std::string& tab_id,
                                 aerie_sidebar::GridKey key) {
  const int focused = IndexOfTile(tab_id);
  if (focused < 0) {
    return;
  }
  const aerie_sidebar::ListKeyResult result = aerie_sidebar::HandleGridKey(
      static_cast<int>(tiles_.size()), favorites_grid_->columns(), focused,
      key);
  if (result.close_focused) {
    CloseTab(tab_id);
    return;
  }
  FocusTile(result.focus_index);
}

void AerieSidebarView::OnTileFocused() {
  UpdateTileTabStop();
}

void AerieSidebarView::UpdateTileTabStop() {
  AerieSidebarTileView* stop = nullptr;
  for (const Tile& tile : tiles_) {
    if (tile.view->HasFocus()) {
      stop = tile.view;
      break;
    }
  }
  if (!stop) {
    for (const Tile& tile : tiles_) {
      if (tile.view->selected()) {
        stop = tile.view;
        break;
      }
    }
  }
  if (!stop && !tiles_.empty()) {
    stop = tiles_.front().view;
  }
  for (const Tile& tile : tiles_) {
    tile.view->SetTabStop(tile.view == stop);
  }
}

void AerieSidebarView::FocusTile(int index) {
  if (index < 0 || index >= static_cast<int>(tiles_.size())) {
    return;
  }
  AerieSidebarTileView* target = tiles_[index].view;
  for (const Tile& tile : tiles_) {
    tile.view->SetTabStop(tile.view == target);
  }
  target->RequestFocus();
}

void AerieSidebarView::FocusTileStop() {
  if (tiles_.empty()) {
    return;
  }
  UpdateTileTabStop();
  for (size_t i = 0; i < tiles_.size(); ++i) {
    if (tiles_[i].view->is_tab_stop()) {
      FocusTile(static_cast<int>(i));
      return;
    }
  }
}

void AerieSidebarView::SelectTab(const std::string& tab_id) {
  if (tabs::TabInterface* tab = FindTab(tab_id)) {
    TabStripModel* model = browser_->GetTabStripModel();
    model->ActivateTabAt(model->GetIndexOfTab(tab));
  }
}

void AerieSidebarView::CloseTab(const std::string& tab_id) {
  tabs::TabInterface* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  // A Pinned or Favorite tab closes as the pinned-close setting says (issue
  // #180): the controller decides, and returns false for any other tab.
  if (auto* pinned = PinnedController()) {
    if (pinned->HandleClose(tab)) {
      return;
    }
  }
  tab->Close();
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
                         self->SyncAll();
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
