// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_hz_chrome/aerie_hz_chrome.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_hz_policy.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

namespace {

using aerie_tab_model::Section;

void CollectTabViews(views::View* root, std::vector<TabView*>* out) {
  if (auto* tab = views::AsViewClass<TabView>(root)) {
    out->push_back(tab);
    return;
  }
  for (views::View* child : root->children()) {
    CollectTabViews(child, out);
  }
}

ui::ImageModel FaviconFor(tabs::TabInterface* tab, SkColor icon_color, int size) {
  if (content::WebContents* contents = tab->GetContents()) {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(contents)) {
      gfx::Image image = driver->GetFavicon();
      if (!image.IsEmpty()) {
        return ui::ImageModel::FromImage(image);
      }
    }
  }
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon, icon_color,
                                        size);
}

}  // namespace

// ---- The Workspace chip ----

AerieHzWorkspaceChip::AerieHzWorkspaceChip(Profile* profile) {
  if (auto* service =
          aerie_tab_model::TabModelServiceFactory::GetForProfile(profile)) {
    model_ = service->model();
    model_->AddObserver(this);
  }
  // Read-only: a label, not a button (the switcher comes with Workspaces).
  GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
  SetProperty(views::kFlexBehaviorKey,
              views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                       views::MaximumFlexSizeRule::kPreferred));
  Refresh();
}

AerieHzWorkspaceChip::~AerieHzWorkspaceChip() {
  if (model_) {
    model_->RemoveObserver(this);
  }
}

void AerieHzWorkspaceChip::Refresh() {
  name_ = model_ ? base::UTF8ToUTF16(model_->workspace().name) : u"";
  GetViewAccessibility().SetName(
      l10n_util::GetStringFUTF16(IDS_AERIE_HZ_WORKSPACE_CHIP, name_));
  InvalidateLayout();
  SchedulePaint();
}

gfx::Size AerieHzWorkspaceChip::CalculatePreferredSize(
    const views::SizeBounds&) const {
  const gfx::FontList font = gfx::FontList().Derive(
      0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM);
  const int text = gfx::GetStringWidth(name_, font);
  return gfx::Size(std::min(text, 110) + 2 * 10, 28);
}

void AerieHzWorkspaceChip::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* provider = GetColorProvider();
  if (!provider) {
    return;
  }
  const SkColor accent = provider->GetColor(ui::kColorAccent);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SkColorSetA(accent, 0x38));
  canvas->DrawRoundRect(GetLocalBounds(), aerie::tokens::kRadiusLg, flags);
  const gfx::FontList font = gfx::FontList().Derive(
      0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM);
  gfx::Rect text_bounds = GetLocalBounds();
  text_bounds.Inset(gfx::Insets::VH(0, 10));
  canvas->DrawStringRectWithFlags(
      name_, font, provider->GetColor(ui::kColorPrimaryForeground), text_bounds,
      gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_ELLIPSIS);
}

void AerieHzWorkspaceChip::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

void AerieHzWorkspaceChip::OnWorkspaceChanged(const aerie_tab_model::Workspace&) {
  Refresh();
}

void AerieHzWorkspaceChip::OnModelLoaded() {
  Refresh();
}

BEGIN_METADATA(AerieHzWorkspaceChip)
END_METADATA

// ---- The Favorites bar ----

AerieHzFavoritesBar::AerieHzFavoritesBar(BrowserView* browser_view,
                                         TabStripRegionView* strip)
    : browser_view_(browser_view), strip_(strip) {
  auto layout = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2);
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  SetLayoutManager(std::move(layout));
  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FAVORITES));
  SetProperty(views::kFlexBehaviorKey,
              views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                       views::MaximumFlexSizeRule::kPreferred));
  if (auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
          browser_view_->GetProfile())) {
    model_ = service->model();
    model_->AddObserver(this);
  }
  browser_view_->browser()->tab_strip_model()->AddObserver(this);
  Rebuild();
}

AerieHzFavoritesBar::~AerieHzFavoritesBar() {
  if (model_) {
    model_->RemoveObserver(this);
  }
  browser_view_->browser()->tab_strip_model()->RemoveObserver(this);
  // Give the Favorites their place in the strip back.
  std::vector<TabView*> tabs;
  CollectTabViews(strip_, &tabs);
  for (TabView* tab : tabs) {
    tab->SetVisible(true);
  }
  strip_->InvalidateLayout();
}

void AerieHzFavoritesBar::OnThemeChanged() {
  views::View::OnThemeChanged();
  Rebuild();
}

void AerieHzFavoritesBar::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<AerieHzFavoritesBar> self) {
                       if (self) {
                         self->rebuild_pending_ = false;
                         self->Rebuild();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void AerieHzFavoritesBar::OnTabAdded(const aerie_tab_model::TabRecord&) {
  ScheduleRebuild();
}
void AerieHzFavoritesBar::OnTabChanged(const aerie_tab_model::TabRecord&) {
  ScheduleRebuild();
}
void AerieHzFavoritesBar::OnTabMoved(const aerie_tab_model::TabRecord&,
                                     const aerie_tab_model::Placement&) {
  ScheduleRebuild();
}
void AerieHzFavoritesBar::OnTabRemoved(const aerie_tab_model::TabRecord&) {
  ScheduleRebuild();
}
void AerieHzFavoritesBar::OnModelLoaded() {
  ScheduleRebuild();
}
void AerieHzFavoritesBar::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  ScheduleRebuild();
}
void AerieHzFavoritesBar::OnTabChangedAt(tabs::TabInterface*, TabChangeType) {
  ScheduleRebuild();
}

void AerieHzFavoritesBar::Rebuild() {
  RemoveAllChildViews();
  if (!model_) {
    return;
  }
  TabStripModel* strip = browser_view_->browser()->tab_strip_model();
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser_view_->GetProfile());
  if (!service) {
    return;
  }
  const auto metrics = aerie_sidebar::GetActiveHzMetrics();
  const int tile = metrics ? metrics->tile : aerie::tokens::kSizeHzTileStrip;
  const int icon = metrics ? metrics->favicon : aerie::tokens::kSizeHzFavicon;
  const ui::ColorProvider* provider = GetColorProvider();

  std::vector<const aerie_tab_model::TabRecord*> favorites;
  for (const auto& [id, record] : model_->tabs()) {
    if (record.section == Section::kFavorite && !record.archived_at &&
        record.workspace_id == model_->workspace().id) {
      favorites.push_back(&record);
    }
  }
  std::sort(favorites.begin(), favorites.end(), [](const auto* a, const auto* b) {
    return a->position < b->position;
  });
  for (const auto* record : favorites) {
    tabs::TabInterface* tab = service->adapter()->TabForRecord(record->id);
    if (!tab) {
      continue;
    }
    const int index = strip->GetIndexOfTab(tab);
    const SkColor muted = provider ? provider->GetColor(ui::kColorSecondaryForeground)
                                   : SK_ColorGRAY;
    auto button = std::make_unique<views::ImageButton>(base::BindRepeating(
        [](BrowserView* view, tabs::TabInterface* tab_ptr) {
          TabStripModel* model = view->browser()->tab_strip_model();
          const int i = model->GetIndexOfTab(tab_ptr);
          if (i >= 0) {
            model->ActivateTabAt(i);
          }
        },
        browser_view_, tab));
    button->SetImageModel(views::Button::STATE_NORMAL,
                          FaviconFor(tab, muted, icon));
    button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    button->SetPreferredSize(gfx::Size(tile, tile));
    const std::u16string title = tab->GetTitle();
    button->GetViewAccessibility().SetName(title);
    button->SetTooltipText(title);
    if (provider) {
      const SkColor base = index == strip->active_index()
                               ? SkColorSetA(provider->GetColor(ui::kColorAccent), 0x38)
                               : SK_ColorTRANSPARENT;
      button->SetBackground(
          views::CreateRoundedRectBackground(base, aerie::tokens::kRadiusMd));
    }
    AddChildView(std::move(button));
  }
  HideFavoritesInStrip();
  PreferredSizeChanged();
}

void AerieHzFavoritesBar::HideFavoritesInStrip() {
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser_view_->GetProfile());
  if (!service || !model_) {
    return;
  }
  std::vector<TabView*> tabs;
  CollectTabViews(strip_, &tabs);
  for (TabView* view : tabs) {
    const tabs::TabInterface* tab = view->GetTabInterface();
    if (!tab) {
      continue;
    }
    const aerie_tab_model::TabRecord* record =
        model_->GetTab(service->adapter()->RecordIdForTab(tab));
    const bool favorite = record && record->section == Section::kFavorite;
    if (view->GetVisible() == favorite) {
      view->SetVisible(!favorite);
    }
  }
}

BEGIN_METADATA(AerieHzFavoritesBar)
END_METADATA

// ---- Wiring ----

namespace aerie_hz_chrome {

void UpdateForLayout(BrowserView* browser_view,
                     TabStripRegionView* horizontal_region,
                     ToolbarView* toolbar) {
  if (!horizontal_region || !toolbar) {
    return;
  }
  const aerie_sidebar::TabLayout layout =
      aerie_sidebar::GetTabLayout(*browser_view->GetProfile()->GetPrefs());
  const bool horizontal = layout != aerie_sidebar::TabLayout::kSidebar;
  const bool two_rows = layout == aerie_sidebar::TabLayout::kHorizontalTwoRows;

  auto* chip = [&]() -> AerieHzWorkspaceChip* {
    for (views::View* child : horizontal_region->children()) {
      if (auto* c = views::AsViewClass<AerieHzWorkspaceChip>(child)) {
        return c;
      }
    }
    return nullptr;
  }();
  if (horizontal && !chip) {
    horizontal_region->AddChildViewAt(
        std::make_unique<AerieHzWorkspaceChip>(browser_view->GetProfile()), 0);
  } else if (!horizontal && chip) {
    horizontal_region->RemoveChildViewT(chip);
  }

  auto* bar = [&]() -> AerieHzFavoritesBar* {
    for (views::View* child : toolbar->children()) {
      if (auto* b = views::AsViewClass<AerieHzFavoritesBar>(child)) {
        return b;
      }
    }
    return nullptr;
  }();
  if (two_rows && !bar) {
    toolbar->AddChildView(
        std::make_unique<AerieHzFavoritesBar>(browser_view, horizontal_region));
  } else if (!two_rows && bar) {
    toolbar->RemoveChildViewT(bar);
  }
}

}  // namespace aerie_hz_chrome
