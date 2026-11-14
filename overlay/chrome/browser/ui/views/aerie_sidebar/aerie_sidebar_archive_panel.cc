// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_panel.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kPanelWidth = aerie::tokens::kSizeSidebarWidth - 2 * aerie::tokens::kSpace16;
constexpr int kListMaxHeight = 320;

struct TimeoutOption {
  const char* value;
  int label_id;
  int command;
};

constexpr TimeoutOption kTimeouts[] = {
    {"12h", IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_12H, 1},
    {"24h", IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_24H, 2},
    {"7d", IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_7D, 3},
    {"never", IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_NEVER, 4},
};

const TimeoutOption& OptionFor(const std::string& value) {
  for (const TimeoutOption& option : kTimeouts) {
    if (value == option.value) {
      return option;
    }
  }
  return kTimeouts[0];
}

int GroupTitleId(aerie_tab_model::ArchiveGroup group) {
  switch (group) {
    case aerie_tab_model::ArchiveGroup::kToday:
      return IDS_AERIE_SIDEBAR_ARCHIVE_GROUP_TODAY;
    case aerie_tab_model::ArchiveGroup::kYesterday:
      return IDS_AERIE_SIDEBAR_ARCHIVE_GROUP_YESTERDAY;
    case aerie_tab_model::ArchiveGroup::kLastWeek:
      return IDS_AERIE_SIDEBAR_ARCHIVE_GROUP_LAST_WEEK;
    case aerie_tab_model::ArchiveGroup::kEarlier:
      return IDS_AERIE_SIDEBAR_ARCHIVE_GROUP_EARLIER;
  }
}

std::u16string TitleOf(const aerie_tab_model::ArchivedRow& row) {
  return base::UTF8ToUTF16(row.title.empty() ? row.url : row.title);
}

}  // namespace

// ---- AerieSidebarArchiveRow -------------------------------------------------

AerieSidebarArchiveRow::AerieSidebarArchiveRow(
    const aerie_tab_model::ArchivedRow& row,
    const std::u16string& workspace_name,
    const aerie_sidebar::SidebarPalette& palette,
    PressedCallback callback,
    AerieSidebarArchivePanel* panel)
    : views::Button(std::move(callback)),
      row_(row),
      title_text_(TitleOf(row)),
      workspace_text_(workspace_name),
      palette_(palette),
      panel_(panel) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(aerie::tokens::kSpace4, aerie::tokens::kSpace8),
      aerie::tokens::kSpace8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* icon = AddChildView(std::make_unique<views::ImageView>());
  icon->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kGlobeIcon, palette_.text_muted, aerie::tokens::kSizeIcon));
  icon->GetViewAccessibility().SetIsIgnored(true);
  auto* title = AddChildView(std::make_unique<views::Label>(title_text_));
  title->SetEnabledColor(palette_.text);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  title->GetViewAccessibility().SetIsIgnored(true);
  layout->SetFlexForView(title, 1);
  auto* workspace =
      AddChildView(std::make_unique<views::Label>(workspace_text_));
  workspace->SetEnabledColor(palette_.text_muted);
  workspace->SetFontList(workspace->font_list().DeriveWithSizeDelta(
      aerie::tokens::kTypeSizeCaption - workspace->font_list().GetFontSize()));
  workspace->GetViewAccessibility().SetIsIgnored(true);

  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(this);
  AerieSidebarFocusRing::Install(this, aerie::tokens::kRadiusMd)
      ->SetColor(palette_.accent);
  SetTooltipText(base::UTF8ToUTF16(row.url));
  GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
  GetViewAccessibility().SetName(l10n_util::GetStringFUTF16(
      IDS_AERIE_SIDEBAR_ARCHIVE_ROW_NAME, title_text_, workspace_text_));
}

AerieSidebarArchiveRow::~AerieSidebarArchiveRow() = default;

bool AerieSidebarArchiveRow::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_DOWN) {
    panel_->FocusNeighbour(this, +1);
    return true;
  }
  if (event.key_code() == ui::VKEY_UP) {
    panel_->FocusNeighbour(this, -1);
    return true;
  }
  if (event.key_code() == ui::VKEY_ESCAPE) {
    panel_->ClosePanel(/*refocus_button=*/true);
    return true;
  }
  return views::Button::OnKeyPressed(event);
}

void AerieSidebarArchiveRow::OnPaintBackground(gfx::Canvas* canvas) {
  const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED ||
                   HasFocus();
  if (!hot) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(palette_.row_hover);
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), aerie::tokens::kRadiusMd,
                        flags);
}

void AerieSidebarArchiveRow::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  SchedulePaint();
}

void AerieSidebarArchiveRow::OnFocus() {
  views::Button::OnFocus();
  ScrollViewToVisible();
  SchedulePaint();
}

void AerieSidebarArchiveRow::OnBlur() {
  views::Button::OnBlur();
  SchedulePaint();
}

BEGIN_METADATA(AerieSidebarArchiveRow)
END_METADATA

// ---- AerieSidebarArchivePanel -----------------------------------------------

// static
AerieSidebarArchivePanel* AerieSidebarArchivePanel::Show(
    views::View* anchor,
    Host* host,
    const aerie_sidebar::SidebarPalette& palette) {
  auto panel =
      base::WrapUnique(new AerieSidebarArchivePanel(anchor, host, palette));
  AerieSidebarArchivePanel* raw = panel.get();
  views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(panel), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
      ->Show();
  // The popover motion: a 4 px drop and a fade on `fast`, `decelerate`. Only
  // the transform and the opacity of the contents' layer change.
  views::View* contents = raw->GetContentsView();
  const bool reduced = aerie::motion::ShouldReduceMotion(host->Prefs());
  const aerie_sidebar::PopoverMotion motion =
      aerie_sidebar::PopoverOpenMotion(reduced);
  contents->SetPaintToLayer();
  contents->layer()->SetFillsBoundsOpaquely(false);
  contents->layer()->SetOpacity(motion.from_opacity);
  aerie::motion::LayerMotion target;
  target.opacity = 1.f;
  if (motion.animate_slide) {
    contents->layer()->SetTransform(
        gfx::Transform::MakeTranslation(0, -motion.drop_px));
    target.transform = gfx::Transform();
  }
  raw->open_motion_ = aerie::motion::AnimateLayer(
      contents->layer(), target, motion.duration, motion.easing, host->Prefs());
  raw->search_->RequestFocus();
  return raw;
}

AerieSidebarArchivePanel::AerieSidebarArchivePanel(
    views::View* anchor,
    Host* host,
    const aerie_sidebar::SidebarPalette& palette)
    : views::BubbleDialogDelegate(anchor,
                                  views::BubbleBorder::BOTTOM_LEFT,
                                  views::BubbleBorder::DIALOG_SHADOW),
      host_(host),
      palette_(palette) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  SetAccessibleTitle(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_PANEL));
  set_fixed_width(kPanelWidth);
  set_margins(gfx::Insets(aerie::tokens::kSpace8));

  auto contents = std::make_unique<views::BoxLayoutView>();
  contents->SetOrientation(views::BoxLayout::Orientation::kVertical);
  contents->SetBetweenChildSpacing(aerie::tokens::kSpace8);
  contents->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Header: the sentence, with the time as a menu button.
  auto* header = contents->AddChildView(std::make_unique<views::BoxLayoutView>());
  header->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  header->SetBetweenChildSpacing(aerie::tokens::kSpace4);
  header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* lead = header->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_HEADER)));
  lead->SetEnabledColor(palette_.text_muted);
  lead->GetViewAccessibility().SetIsIgnored(true);
  timeout_button_ = header->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&AerieSidebarArchivePanel::ShowTimeoutMenu,
                          base::Unretained(this)),
      std::u16string()));
  timeout_button_->SetEnabledTextColors(palette_.text);
  timeout_button_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(timeout_button_);
  AerieSidebarFocusRing::Install(timeout_button_, aerie::tokens::kRadiusMd)
      ->SetColor(palette_.accent);
  timeout_button_->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
  UpdateTimeoutLabel();

  search_ = contents->AddChildView(std::make_unique<views::Textfield>());
  search_->set_controller(this);
  search_->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_SEARCH));
  search_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_SEARCH));
  search_->GetViewAccessibility().SetRole(ax::mojom::Role::kSearchBox);

  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  list->GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  list->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_LIST));
  scroll_ = contents->AddChildView(std::make_unique<views::ScrollView>());
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->ClipHeightTo(0, kListMaxHeight);
  list_ = scroll_->SetContents(std::move(list));
  SetContentsView(std::move(contents));
  SetInitiallyFocusedView(search_);
  BuildList();
  RegisterWindowClosingCallback(base::BindOnce(
      &AerieSidebarArchivePanel::OnClosing, base::Unretained(this)));
}

AerieSidebarArchivePanel::~AerieSidebarArchivePanel() {
  open_motion_.Abort();
}

void AerieSidebarArchivePanel::UpdateTimeoutLabel() {
  const TimeoutOption& option = OptionFor(host_->ArchiveAfter());
  const std::u16string time = l10n_util::GetStringUTF16(option.label_id);
  timeout_button_->SetText(time);
  timeout_button_->GetViewAccessibility().SetName(
      l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_NAME, time));
}

void AerieSidebarArchivePanel::BuildList() {
  list_->RemoveAllChildViews();
  const std::string query = base::UTF16ToUTF8(query_);
  shown_ = aerie_tab_model::SearchArchive(host_->ArchivedRows(), query);
  const auto sections = aerie_tab_model::GroupArchive(shown_, host_->Now());
  if (sections.empty()) {
    auto* empty = list_->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(query.empty()
                                      ? IDS_AERIE_SIDEBAR_ARCHIVE_EMPTY
                                      : IDS_AERIE_SIDEBAR_ARCHIVE_NO_MATCHES)));
    empty->SetEnabledColor(palette_.text_muted);
    empty->SetMultiLine(true);
    empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    return;
  }
  for (const auto& section : sections) {
    auto* heading = list_->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(GroupTitleId(section.group))));
    heading->SetEnabledColor(palette_.text_muted);
    heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading->SetFontList(heading->font_list().DeriveWithSizeDelta(
        aerie::tokens::kTypeSizeCaption - heading->font_list().GetFontSize()));
    heading->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(aerie::tokens::kSpace8, aerie::tokens::kSpace8, 0, 0)));
    heading->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
    for (const auto& archived : section.rows) {
      list_->AddChildView(std::make_unique<AerieSidebarArchiveRow>(
          archived, host_->WorkspaceName(archived.workspace_id), palette_,
          base::BindRepeating(
              [](base::WeakPtr<AerieSidebarArchivePanel> self, std::string id) {
                if (!self) {
                  return;
                }
                for (size_t i = 0; i < self->rows().size(); ++i) {
                  if (self->rows()[i]->archived().id == id) {
                    self->ChooseRow(i);
                    return;
                  }
                }
              },
              GetWeakPtr(), archived.id),
          this));
    }
  }
}

std::vector<AerieSidebarArchiveRow*> AerieSidebarArchivePanel::rows() const {
  std::vector<AerieSidebarArchiveRow*> out;
  for (views::View* child : list_->children()) {
    if (child->GetClassName() == std::string_view("AerieSidebarArchiveRow")) {
      out.push_back(static_cast<AerieSidebarArchiveRow*>(child));
    }
  }
  return out;
}

std::vector<std::u16string> AerieSidebarArchivePanel::group_titles() const {
  std::vector<std::u16string> out;
  for (views::View* child : list_->children()) {
    if (child->GetClassName() == std::string_view("Label") &&
        child->GetViewAccessibility().GetCachedRole() ==
            ax::mojom::Role::kHeading) {
      out.emplace_back(static_cast<views::Label*>(child)->GetText());
    }
  }
  return out;
}

void AerieSidebarArchivePanel::ChooseRow(size_t index) {
  const auto all = rows();
  if (index >= all.size() || closed_) {
    return;
  }
  const aerie_tab_model::ArchivedRow archived = all[index]->archived();
  const gfx::Rect in_screen = all[index]->GetBoundsInScreen();
  ClosePanel(/*refocus_button=*/true);
  host_->RestoreArchived(archived, in_screen);
}

void AerieSidebarArchivePanel::ClosePanel(bool refocus_button) {
  if (closed_) {
    return;
  }
  refocus_ = refocus_button;
  views::Widget* widget = GetWidget();
  if (widget && !widget->IsClosed()) {
    // No closing animation: the widget goes at once.
    widget->Close();
  } else {
    OnClosing();
  }
}

void AerieSidebarArchivePanel::OnClosing() {
  if (closed_) {
    return;
  }
  closed_ = true;
  host_->OnArchivePanelClosed(refocus_);
}

void AerieSidebarArchivePanel::ContentsChanged(
    views::Textfield*,
    const std::u16string& new_contents) {
  query_ = new_contents;
  BuildList();
  list_->InvalidateLayout();
}

bool AerieSidebarArchivePanel::HandleKeyEvent(views::Textfield*,
                                              const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    ClosePanel(/*refocus_button=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_DOWN) {
    const auto all = rows();
    if (!all.empty()) {
      all.front()->RequestFocus();
    }
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    ChooseRow(0);
    return true;
  }
  return false;
}

void AerieSidebarArchivePanel::FocusNeighbour(AerieSidebarArchiveRow* from,
                                              int direction) {
  const auto all = rows();
  const auto it = std::find(all.begin(), all.end(), from);
  if (it == all.end()) {
    return;
  }
  const int index = static_cast<int>(it - all.begin()) + direction;
  if (index < 0) {
    search_->RequestFocus();
  } else if (index < static_cast<int>(all.size())) {
    all[index]->RequestFocus();
  }
}

void AerieSidebarArchivePanel::ShowTimeoutMenu() {
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (const TimeoutOption& option : kTimeouts) {
    menu_model_->AddCheckItem(option.command,
                              l10n_util::GetStringUTF16(option.label_id));
  }
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(GetWidget(), /*button_controller=*/nullptr,
                          timeout_button_->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

bool AerieSidebarArchivePanel::IsCommandIdChecked(int command_id) const {
  return OptionFor(host_->ArchiveAfter()).command == command_id;
}

void AerieSidebarArchivePanel::ExecuteCommand(int command_id, int) {
  for (const TimeoutOption& option : kTimeouts) {
    if (option.command == command_id) {
      host_->SetArchiveAfter(option.value);
      UpdateTimeoutLabel();
      return;
    }
  }
}
