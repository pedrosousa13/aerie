// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"

#include <cmath>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_fade.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_field.h"
#include "chrome/browser/ui/views/aerie_sidebar/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_drag.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace {
constexpr int kFaviconSize = aerie::tokens::kSizeIcon;
// The moon badge of a sleeping tab (issue #179), from the tokens.
constexpr int kSleepBadgeSize = aerie::tokens::kSizeSleepBadge;
constexpr int kSleepBadgeOffset = aerie::tokens::kSizeSleepBadgeOffset;
// The square of the close button and of the funnel that opens a folder's sheet.
constexpr int kRowButtonSize = 20;


constexpr int kDropLayersSlackMs = AerieSidebarRowView::kDropLayersSlackMs;

// The row height animates on the design's `standard` curve, which is exactly
// this Tween type (aerie_motion.h).
constexpr gfx::Tween::Type kHeightTween = gfx::Tween::FAST_OUT_SLOW_IN_3;

// A transform that scales `scale` about the middle of a `size` box.
gfx::Transform ScaleAboutCenter(const gfx::Size& size, float scale) {
  return gfx::TransformAboutPivot(
      gfx::PointF(size.width() / 2.0f, size.height() / 2.0f),
      gfx::Transform::MakeScale(scale));
}

// The favicon and the moon badge that sits on its lower corner. The badge is
// beside the favicon, not in it, so it keeps its full colour while the favicon
// fades.
class FaviconHolder : public views::View {
  METADATA_HEADER(FaviconHolder, views::View)

 public:
  FaviconHolder(views::ImageView* favicon, views::ImageView* badge)
      : favicon_(AddChildView(favicon)), badge_(AddChildView(badge)) {}

  // The icon size of the density (issue #184).
  void SetIconSize(int size) {
    if (size_ != size) {
      size_ = size;
      favicon_->SetImageSize(gfx::Size(size_, size_));
      PreferredSizeChanged();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(size_, size_);
  }
  void Layout(PassKey) override {
    favicon_->SetBoundsRect(gfx::Rect(0, 0, size_, size_));
    badge_->SetBoundsRect(gfx::Rect(kSleepBadgeOffset, kSleepBadgeOffset,
                                    kSleepBadgeSize, kSleepBadgeSize));
  }

 private:
  int size_ = kFaviconSize;
  raw_ptr<views::ImageView> favicon_;
  raw_ptr<views::ImageView> badge_;
};

BEGIN_METADATA(FaviconHolder)
END_METADATA

bool IsCloseShortcut(const ui::KeyEvent& event) {
  if (event.key_code() != ui::VKEY_W) {
    return false;
  }
#if BUILDFLAG(IS_MAC)
  return event.IsCommandDown() && !event.IsControlDown();
#else
  return event.IsControlDown() && !event.IsShiftDown() && !event.IsAltDown();
#endif
}

// Alt+Arrow, with no other modifier (issue #182).
bool IsMoveShortcut(const ui::KeyEvent& event) {
  if (!event.IsAltDown() || event.IsControlDown() || event.IsShiftDown() ||
      event.IsCommandDown()) {
    return false;
  }
  return event.key_code() == ui::VKEY_UP || event.key_code() == ui::VKEY_DOWN ||
         event.key_code() == ui::VKEY_LEFT ||
         event.key_code() == ui::VKEY_RIGHT;
}
}  // namespace

AerieSidebarRowView::AerieSidebarRowView(
    Callbacks callbacks,
    const aerie_sidebar::SidebarPalette& palette)
    : callbacks_(std::move(callbacks)), palette_(palette) {
  // Everything that is drawn is in the content view: it keeps the full row
  // height while the row itself grows or collapses (and clips), and its layer
  // is the one that scales, slides and fades.
  content_ = AddChildView(std::make_unique<views::View>());
  auto* layout = content_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, aerie::tokens::kSpace8), aerie::tokens::kSpace8));
  content_layout_ = layout;
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  content_->GetViewAccessibility().SetIsIgnored(true);

  auto favicon = std::make_unique<views::ImageView>();
  favicon->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon->GetViewAccessibility().SetIsIgnored(true);
  auto badge = std::make_unique<views::ImageView>();
  badge->SetImageSize(gfx::Size(kSleepBadgeSize, kSleepBadgeSize));
  badge->GetViewAccessibility().SetIsIgnored(true);
  badge->SetVisible(false);
  favicon_ = favicon.get();
  sleep_badge_ = badge.get();
  favicon_holder_ = content_->AddChildView(
      std::make_unique<FaviconHolder>(favicon.release(), badge.release()));

  title_ = content_->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetEnabledColor(palette_.text);
  title_->GetViewAccessibility().SetIsIgnored(true);
  layout->SetFlexForView(title_, 1);

  // The pencil of a renamed tab (issue #145). It resets the name. Like the
  // back button it is a pointer affordance: the keyboard way is the context
  // menu ("Reset name") or F2 and an empty name.
  pencil_ =
      content_->AddChildView(views::CreateVectorImageButton(base::BindRepeating(
          [](AerieSidebarRowView* row) {
            if (row->callbacks_.reset_name) {
              row->callbacks_.reset_name.Run();
            }
          },
          base::Unretained(this))));
  pencil_->SetPreferredSize(gfx::Size(aerie::tokens::kSizePencilButton,
                                      aerie::tokens::kSizePencilButton));
  pencil_->SetVisible(false);
  pencil_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  pencil_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(pencil_);
  pencil_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RESET_NAME));

  // The caption under the rename field. It is a child of the row, below the
  // content, and only shows while the field is open.
  rename_caption_ = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_CAPTION),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  rename_caption_->SetFontList(rename_caption_->font_list().DeriveWithSizeDelta(
      aerie::tokens::kTypeSizeCaption -
      rename_caption_->font_list().GetFontSize()));
  rename_caption_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  rename_caption_->SetMultiLine(true);
  rename_caption_->SetVisible(false);
  rename_caption_->GetViewAccessibility().SetIsIgnored(true);

  // The "Back to pinned page" button, shown only on a pinned row that has left
  // its page. It is not a focus stop: the row's own keys are the keyboard way
  // (the context menu has no entry for it either; the button is a pointer
  // affordance, and a screen reader reaches it by its name).
  back_to_pinned_ = content_->AddChildView(views::CreateVectorImageButton(
      base::BindRepeating(
          [](AerieSidebarRowView* row) {
            if (row->callbacks_.back_to_pinned) {
              row->callbacks_.back_to_pinned.Run();
            }
          },
          base::Unretained(this))));
  back_to_pinned_->SetPreferredSize(gfx::Size(aerie::tokens::kSizeBackButton, aerie::tokens::kSizeBackButton));
  back_to_pinned_->SetVisible(false);
  back_to_pinned_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  back_to_pinned_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(back_to_pinned_);
  back_to_pinned_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));
  back_to_pinned_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));

  close_button_ = content_->AddChildView(views::CreateVectorImageButton(
      base::BindRepeating(
          [](AerieSidebarRowView* row) { row->callbacks_.close.Run(); },
          base::Unretained(this))));
  close_button_->SetPreferredSize(gfx::Size(kRowButtonSize, kRowButtonSize));
  close_button_->SetVisible(false);
  close_button_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  close_button_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(close_button_);
  close_button_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));
  close_button_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));

  // The close button is a child: moving onto it must not count as leaving the
  // row, or the button would hide under the pointer.
  SetNotifyEnterExitOnChild(true);
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  // The focus ring is size-focus-ring-width thick and sits
  // size-focus-ring-offset away from the row (docs/sidebar-design.md,
  // "Rows and tiles"). Chromium's own ring takes only a colour role, so the
  // row draws it in the Workspace accent.
  AerieSidebarFocusRing::Install(this, aerie::tokens::kRadiusMd);
  GetViewAccessibility().SetRole(ax::mojom::Role::kTreeItem);
  GetViewAccessibility().SetHierarchicalLevel(1);
  GetViewAccessibility().SetIsSelected(false);
  GetViewAccessibility().SetName(std::u16string());
  set_context_menu_controller(this);
  // Virtual hooks resolve to this class here; a subclass must call
  // SetPalette() and UpdateName() again at the end of its constructor.
  SetPalette(palette_);
}

AerieSidebarRowView::~AerieSidebarRowView() {
  // A rename that ends with the row (tab closed, resync, folder deleted) still
  // ends: the sidebar looks again, once the row is out of its lists, so that
  // the peek's rename hold is not left held.
  if (renaming_ && callbacks_.rename_state_changed) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, callbacks_.rename_state_changed);
  }
  // The field's blur must not reach a row that is going away.
  renaming_ = false;
  weak_factory_.InvalidateWeakPtrs();
  pencil_motion_.Abort();
  field_motion_.Abort();
  title_motion_.Abort();
  // A row that goes away mid-motion must not leave an animation behind.
  content_motion_.Abort();
  row_motion_.Abort();
  lift_motion_.Abort();
  sleep_motion_.Abort();
}

void AerieSidebarRowView::SetTabStop(bool tab_stop) {
  if (closing_) {
    return;
  }
  SetFocusBehavior(tab_stop ? views::View::FocusBehavior::ALWAYS
                            : views::View::FocusBehavior::ACCESSIBLE_ONLY);
}

bool AerieSidebarRowView::is_tab_stop() const {
  return GetFocusBehavior() == views::View::FocusBehavior::ALWAYS;
}

void AerieSidebarRowView::SetPalette(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  title_->SetEnabledColor(palette_.text);
  // The close icon is muted at rest and the text colour under the pointer
  // (the mockup's `.ibtn`).
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_DISABLED}) {
    close_button_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kCloseSmallIcon,
                                              palette_.text_muted, icon_size_));
  }
  for (views::Button::ButtonState state :
       {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
    close_button_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kCloseSmallIcon,
                                              palette_.text, icon_size_));
  }
  views::InkDrop::Get(close_button_)->SetBaseColor(palette_.text);
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_DISABLED}) {
    back_to_pinned_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kUndoIcon,
                                              palette_.text_muted, icon_size_));
  }
  for (views::Button::ButtonState state :
       {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
    back_to_pinned_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kUndoIcon,
                                              palette_.text, icon_size_));
  }
  views::InkDrop::Get(back_to_pinned_)->SetBaseColor(palette_.text);
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_DISABLED}) {
    pencil_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(
                   vector_icons::kEditIcon, palette_.text_muted, icon_size_));
  }
  for (views::Button::ButtonState state :
       {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
    pencil_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kEditIcon,
                                              palette_.text, icon_size_));
  }
  views::InkDrop::Get(pencil_)->SetBaseColor(palette_.text);
  rename_caption_->SetEnabledColor(palette_.text_muted);
  StyleRenameField();
  AerieSidebarFocusRing::Get(this)->SetColor(palette_.accent);
  sleep_badge_->SetImage(ui::ImageModel::FromVectorIcon(
      aerie_sidebar::kAerieSleepBadgeIcon, palette_.text, kSleepBadgeSize));
  ApplyKindPalette();
  UpdateState();
}

void AerieSidebarRowView::SetBackToPinnedVisible(bool visible) {
  back_to_pinned_wanted_ = visible;
  UpdateTrailing();
}

void AerieSidebarRowView::UpdateTrailing() {
  // The rail shows icons only: no trailing item (issue #184).
  back_to_pinned_->SetVisible(back_to_pinned_wanted_ && !closing_ &&
                              !renaming_ && !icons_only_);
  pencil_->SetVisible(renamed_ && !closing_ && !renaming_ && !icons_only_);
  close_button_->SetVisible(ShowsCloseButton() && !closing_ && !renaming_ &&
                            !icons_only_ && (hovered_ || HasFocus()));
}

void AerieSidebarRowView::SetDensity(
    const aerie_sidebar::DensitySizes& sizes) {
  row_height_ = sizes.row_height;
  icon_size_ = sizes.row_icon;
  icons_only_ = sizes.icons_only;
  static_cast<FaviconHolder*>(favicon_holder_.get())->SetIconSize(icon_size_);
  favicon_->SetImageSize(gfx::Size(icon_size_, icon_size_));
  // The title is hidden in the rail but the row keeps its full name for a
  // screen reader: the name is set on the row, not read from the label.
  title_->SetVisible(!icons_only_);
  content_layout_->set_inside_border_insets(
      icons_only_ ? gfx::Insets() : gfx::Insets::VH(0, aerie::tokens::kSpace8));
  content_layout_->set_main_axis_alignment(
      icons_only_ ? views::BoxLayout::MainAxisAlignment::kCenter
                  : views::BoxLayout::MainAxisAlignment::kStart);
  SetPalette(palette_);
  UpdateTrailing();
  OnDensityChanged();
  InvalidateLayout();
  content_->InvalidateLayout();
  PreferredSizeChanged();
}

void AerieSidebarRowView::SetCloseTooltip(const std::u16string& text) {
  close_button_->SetTooltipText(text);
  close_button_->GetViewAccessibility().SetName(text);
}

void AerieSidebarRowView::SetTitle(const std::u16string& title) {
  title_->SetText(title);
  title_text_ = title;
  UpdateName();
}

bool AerieSidebarRowView::is_folder() const {
  return false;
}

bool AerieSidebarRowView::ShowsCloseButton() const {
  return true;
}

int AerieSidebarRowView::RenameFieldNameId() const {
  return IDS_AERIE_SIDEBAR_RENAME_FIELD_NAME;
}

int AerieSidebarRowView::RenameCaptionId() const {
  return IDS_AERIE_SIDEBAR_RENAME_CAPTION;
}

bool AerieSidebarRowView::IsEditFolderClick(const ui::MouseEvent& event) const {
  return false;
}

void AerieSidebarRowView::UpdateName() {
  GetViewAccessibility().SetName(AccessibleName());
}

std::u16string AerieSidebarRowView::AccessibleName() const {
  // The shown title, then ", asleep", then ", renamed".
  std::u16string name = title_text_;
  if (sleeping_) {
    name = l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_ASLEEP_NAME, name);
  }
  if (renamed_) {
    name = l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_RENAMED_NAME, name);
  }
  return name;
}

void AerieSidebarRowView::SetRenamed(bool renamed,
                                     const std::u16string& pencil_tooltip,
                                     const PrefService* reduced_motion_prefs) {
  pencil_->SetTooltipText(pencil_tooltip);
  if (renamed_applied_ && renamed_ == renamed) {
    return;
  }
  const bool animate = renamed_applied_ && renamed && reduced_motion_prefs;
  renamed_applied_ = true;
  renamed_ = renamed;
  UpdateName();
  UpdateTrailing();
  pencil_motion_.Abort();
  if (!renamed_) {
    if (pencil_->layer()) {
      pencil_->DestroyLayer();
    }
    return;
  }
  if (!animate) {
    return;
  }
  // The pencil fades in: opacity only, on `xfast`.
  pencil_motion_ = FadeRenameViewIn(pencil_, reduced_motion_prefs);
}

AerieSidebarRenameField* AerieSidebarRowView::rename_field() const {
  return rename_field_;
}

void AerieSidebarRowView::EnsureRenameField() {
  if (rename_field_) {
    return;
  }
  auto field = std::make_unique<AerieSidebarRenameField>(base::BindRepeating(
      &AerieSidebarRowView::OnRenameBlur, weak_factory_.GetWeakPtr()));
  field->set_controller(this);
  field->SetVisible(false);
  field->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(RenameFieldNameId()));
  // The caption is the field's description.
  field->GetViewAccessibility().SetDescription(
      l10n_util::GetStringUTF16(RenameCaptionId()));
  views::FocusRing::Remove(field.get());
  // Right after the title, so that it takes the title's place in the layout.
  const std::optional<size_t> title_index = content_->GetIndexOf(title_);
  rename_field_ = content_->AddChildViewAt(std::move(field),
                                           title_index ? *title_index + 1 : 0);
  static_cast<views::BoxLayout*>(content_->GetLayoutManager())
      ->SetFlexForView(rename_field_, 1);
  StyleRenameField();
}

void AerieSidebarRowView::StyleRenameField() {
  if (!rename_field_) {
    return;
  }
  rename_field_->SetPaletteColors(palette_);
}

void AerieSidebarRowView::BeginRename(const std::u16string& initial_text,
                                      const PrefService* reduced_motion_prefs) {
  if (closing_ || renaming_) {
    return;
  }
  EnsureRenameField();
  renaming_ = true;
  if (callbacks_.rename_state_changed) {
    callbacks_.rename_state_changed.Run();
  }
  rename_field_->SetText(initial_text);
  rename_field_->SetVisible(true);
  rename_caption_->SetVisible(true);
  // The title leaves the layout at once but stays where it was while it fades
  // out, under the field that fades in: one cross-fade, opacity only. It takes
  // no event and the screen reader does not see it.
  title_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  title_->SetCanProcessEventsWithinSubtree(false);
  title_motion_.Abort();
  title_motion_ = FadeRenameViewOut(title_, reduced_motion_prefs);
  UpdateTrailing();
  UpdateCaptionHeight();
  PreferredSizeChanged();
  field_motion_.Abort();
  field_motion_ = FadeRenameViewIn(rename_field_, reduced_motion_prefs);
  rename_field_->RequestFocus();
  rename_field_->SelectAll(false);
}

void AerieSidebarRowView::EndRename(bool commit, bool refocus) {
  if (!renaming_) {
    return;
  }
  renaming_ = false;
  if (callbacks_.rename_state_changed) {
    // After the rest: the callback only reads state.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, callbacks_.rename_state_changed);
  }
  const std::u16string text(rename_field_->GetText());
  field_motion_.Abort();
  title_motion_.Abort();
  rename_field_->SetVisible(false);
  rename_caption_->SetVisible(false);
  title_->ClearProperty(views::kViewIgnoredByLayoutKey);
  title_->SetCanProcessEventsWithinSubtree(true);
  if (title_->layer()) {
    title_->layer()->SetOpacity(1.f);
  }
  UpdateTrailing();
  PreferredSizeChanged();
  if (refocus) {
    RequestFocus();
  }
  if (commit && callbacks_.commit_rename) {
    // The commit can rebuild the rows, and this runs inside the field's own
    // OnBlur or HandleKeyEvent: it goes to a task, so that no row or field is
    // destroyed under the call that is still on the stack.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&AerieSidebarRowView::RunCommitRename,
                                  weak_factory_.GetWeakPtr(), text));
  }
}

void AerieSidebarRowView::RunCommitRename(const std::u16string& text) {
  if (callbacks_.commit_rename) {
    callbacks_.commit_rename.Run(text);
  }
}

void AerieSidebarRowView::UpdateCaptionHeight() {
  if (!rename_field_ || !rename_caption_) {
    return;
  }
  // The field's width is known once the content is laid out.
  content_->SetBoundsRect(gfx::Rect(0, 0, width(), row_height_));
  LayoutKindViews();
  content_->DeprecatedLayoutImmediately();
  const int width_now = rename_field_->width();
  caption_height_ = width_now > 0
                        ? rename_caption_->GetHeightForWidth(width_now)
                        : aerie::tokens::kTypeLineCaption;
}

void AerieSidebarRowView::CancelRename() {
  EndRename(/*commit=*/false, /*refocus=*/false);
}

void AerieSidebarRowView::OnRenameBlur() {
  // Focus loss keeps the name (decision for #145).
  if (renaming_) {
    EndRename(/*commit=*/true, /*refocus=*/false);
  }
}

void AerieSidebarRowView::ContentsChanged(views::Textfield* sender,
                                          const std::u16string& new_contents) {
  // The field holds at most kMaxManualNameLength characters.
  if (new_contents.size() > aerie_tab_model::kMaxManualNameLength) {
    sender->SetText(aerie_tab_model::CutToManualNameLength(new_contents));
  }
}

bool AerieSidebarRowView::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    EndRename(/*commit=*/true, /*refocus=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    EndRename(/*commit=*/false, /*refocus=*/true);
    return true;
  }
  return false;
}

void AerieSidebarRowView::SetSleeping(bool sleeping,
                                      const PrefService* reduced_motion_prefs) {
  if (sleep_look_applied_ && sleeping_ == sleeping) {
    return;
  }
  const bool animate = sleep_look_applied_ && sleeping && reduced_motion_prefs;
  sleep_look_applied_ = true;
  sleeping_ = sleeping;
  UpdateName();
  sleep_badge_->SetVisible(sleeping);
  ApplySleepLook(animate, reduced_motion_prefs);
}

void AerieSidebarRowView::ApplySleepLook(bool animate,
                                         const PrefService* prefs) {
  sleep_motion_.Abort();
  desaturate_timer_.Stop();
  if (!sleeping_) {
    // The favicon of an awake tab has no layer, as before.
    if (favicon_->layer()) {
      favicon_->DestroyLayer();
    }
    return;
  }
  if (!favicon_->layer()) {
    favicon_->SetPaintToLayer();
    favicon_->layer()->SetFillsBoundsOpaquely(false);
  }
  ui::Layer* layer = favicon_->layer();
  const aerie_sidebar::SleepMotion motion = aerie_sidebar::SleepFaviconMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  if (!animate || !motion.animate_fade) {
    // An instant swap: both looks at once.
    layer->SetOpacity(motion.asleep_opacity);
    layer->SetLayerGrayscale(1.f);
    return;
  }
  layer->SetOpacity(1.f);
  layer->SetLayerGrayscale(0.f);
  aerie::motion::LayerMotion target;
  target.opacity = motion.asleep_opacity;
  sleep_motion_ = aerie::motion::AnimateLayer(layer, target, motion.duration,
                                              motion.easing, prefs);
  // The desaturate is a static filter that follows the fade; it is not
  // animated.
  desaturate_timer_.Start(
      FROM_HERE,
      base::Milliseconds(aerie::tokens::GetDurationMs(motion.duration, false) +
                         kDropLayersSlackMs),
      base::BindOnce(
          [](AerieSidebarRowView* row) {
            if (row->sleeping_ && row->favicon_->layer()) {
              row->favicon_->layer()->SetLayerGrayscale(1.f);
            }
          },
          base::Unretained(this)));
}

void AerieSidebarRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!closing_ && callbacks_.context_menu) {
    callbacks_.context_menu.Run(point, source_type);
  }
}

void AerieSidebarRowView::SetFavicon(const ui::ImageModel& favicon) {
  favicon_->SetImage(favicon);
}

void AerieSidebarRowView::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  GetViewAccessibility().SetIsSelected(selected);
  UpdateState();
}

void AerieSidebarRowView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  // The content keeps its full height while the row grows or collapses.
  content_->SetBoundsRect(gfx::Rect(0, 0, width(), row_height_));
  if (renaming_ && rename_field_) {
    // The caption sits under the field, as wide as the field.
    content_->DeprecatedLayoutImmediately();
    const int caption_height =
        rename_caption_->GetHeightForWidth(rename_field_->width());
    rename_caption_->SetBoundsRect(gfx::Rect(rename_field_->x(), row_height_,
                                             rename_field_->width(),
                                             caption_height));
    if (caption_height != caption_height_) {
      // The caption wraps to another number of lines at this width.
      caption_height_ = caption_height;
      PreferredSizeChanged();
    }
  }
}

std::u16string AerieSidebarRowView::GetRenderedTooltipText(
    const gfx::Point& p) const {
  // The tooltip tells what the row cuts off: it shows only for an elided title.
  return title_->IsDisplayTextTruncated() ? std::u16string(title_->GetText())
                                          : std::u16string();
}

gfx::Size AerieSidebarRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // While the field is open the row is taller by the caption under it. That
  // is a layout change on a user action, not an animation.
  const int caption_block =
      renaming_ ? caption_height_ + aerie::tokens::kSpace4 : 0;
  return gfx::Size(
      available_size.width().value_or(aerie::tokens::kSizeSidebarMin),
      static_cast<int>(std::lround(row_height_ * height_fraction_)) +
          caption_block);
}

bool AerieSidebarRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (closing_ || renaming_) {
    return false;
  }
  suppress_release_ = false;
  pressed_ = event.IsOnlyLeftMouseButton();
  drag_cancelled_ = false;
  press_point_ = event.root_location();
  // A double-click on a folder's icon opens the folder editor (issue #139).
  if (IsEditFolderClick(event)) {
    pressed_ = false;
    suppress_release_ = true;
    callbacks_.edit_folder.Run();
    return true;
  }
  // A double-click on the title starts a rename (issue #145).
  if (event.IsOnlyLeftMouseButton() && event.GetClickCount() == 2 &&
      callbacks_.begin_rename) {
    gfx::Point in_title = event.location();
    views::View::ConvertPointToTarget(this, title_, &in_title);
    if (title_->GetVisible() && title_->GetLocalBounds().Contains(in_title)) {
      pressed_ = false;
      suppress_release_ = true;
      callbacks_.begin_rename.Run();
      return true;
    }
  }
  return event.IsOnlyLeftMouseButton() || event.IsOnlyMiddleMouseButton();
}

bool AerieSidebarRowView::OnMouseDragged(const ui::MouseEvent& event) {
  if (closing_ || !pressed_) {
    return false;
  }
  // The root location does not move with the row's own layer, so the travel is
  // the pointer's, not the pointer's relative to a row that is following it.
  const gfx::Point point = event.root_location();
  const int dx = point.x() - press_point_.x();
  const int dy = point.y() - press_point_.y();
  if (!dragging_) {
    if (!aerie_sidebar::ExceedsDragThreshold(dx, dy)) {
      return true;
    }
    if (!callbacks_.drag_begin || !callbacks_.drag_begin.Run()) {
      return true;
    }
    dragging_ = true;
    // The most recent accelerator for Esc is tried first, so the drag takes
    // it before the browser's own.
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  }
  last_point_ = point;
  callbacks_.drag_update.Run(point);
  return true;
}

void AerieSidebarRowView::OnMouseReleased(const ui::MouseEvent& event) {
  pressed_ = false;
  if (suppress_release_) {
    // The release of the double-click that opened the field: selecting the tab
    // now would take focus from it.
    suppress_release_ = false;
    return;
  }
  if (dragging_) {
    const views::Widget* widget = GetWidget();
    const bool outside =
        widget && !widget->GetRootView()->GetLocalBounds().Contains(
                      event.root_location());
    FinishDrag(event.root_location(), /*cancelled=*/false, outside);
    return;
  }
  if (drag_cancelled_) {
    drag_cancelled_ = false;
    return;
  }
  if (closing_ || !HitTestPoint(event.location())) {
    return;
  }
  if (event.IsOnlyLeftMouseButton()) {
    callbacks_.select.Run();
  } else if (event.IsOnlyMiddleMouseButton()) {
    callbacks_.close.Run();
  }
}

void AerieSidebarRowView::OnMouseCaptureLost() {
  pressed_ = false;
  if (dragging_) {
    FinishDrag(last_point_, /*cancelled=*/true, /*outside=*/false);
  }
}

bool AerieSidebarRowView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (!dragging_ || accelerator.key_code() != ui::VKEY_ESCAPE) {
    return false;
  }
  pressed_ = false;
  drag_cancelled_ = true;
  FinishDrag(last_point_, /*cancelled=*/true, /*outside=*/false);
  return true;
}

void AerieSidebarRowView::FinishDrag(const gfx::Point& point,
                                     bool cancelled,
                                     bool outside) {
  dragging_ = false;
  drag_cursor_.reset();
  RemoveAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  callbacks_.drag_end.Run(point, cancelled, outside);
}

ui::Cursor AerieSidebarRowView::GetCursor(const ui::MouseEvent& event) {
  if (dragging_ && drag_cursor_) {
    return *drag_cursor_;
  }
  return views::View::GetCursor(event);
}

void AerieSidebarRowView::SetDragCursor(const ui::Cursor& cursor) {
  drag_cursor_ = cursor;
}

AerieSidebarDragContent AerieSidebarRowView::GetDragContent() const {
  AerieSidebarDragContent content;
  content.title = title_text_;
  content.icon = favicon_->GetImageModel();
  content.indent = level_ * aerie::tokens::kSizeIndent;
  return content;
}

void AerieSidebarRowView::SetDragHidden(bool hidden) {
  drag_hidden_ = hidden;
  if (hidden) {
    EnsureDragLayers();
    layer()->SetOpacity(0.f);
    return;
  }
  if (layer()) {
    layer()->SetOpacity(1.f);
  }
  DropDragLayers();
}

void AerieSidebarRowView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateState();
}

void AerieSidebarRowView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateState();
}

bool AerieSidebarRowView::OnKeyPressed(const ui::KeyEvent& event) {
  // While the field is open its keys are its own: Backspace in the field must
  // never reach "back to pinned page" (issue #180), nor Delete close the tab.
  if (closing_ || renaming_) {
    return false;
  }
  if (IsCloseShortcut(event)) {
    callbacks_.key.Run(aerie_sidebar::ListKey::kDelete);
    return true;
  }
  // Alt+Arrow moves the row (issue #182).
  if (IsMoveShortcut(event) && callbacks_.move) {
    aerie_sidebar::MoveKey key = aerie_sidebar::MoveKey::kUp;
    switch (event.key_code()) {
      case ui::VKEY_DOWN:
        key = aerie_sidebar::MoveKey::kDown;
        break;
      case ui::VKEY_LEFT:
        key = aerie_sidebar::MoveKey::kOutdent;
        break;
      case ui::VKEY_RIGHT:
        key = aerie_sidebar::MoveKey::kIndent;
        break;
      default:
        break;
    }
    callbacks_.move.Run(key);
    return true;
  }
  if (event.IsControlDown() || event.IsAltDown() || event.IsCommandDown()) {
    return false;
  }
  switch (event.key_code()) {
    case ui::VKEY_F2:
      // Rename (issue #145).
      if (callbacks_.begin_rename && !event.IsShiftDown()) {
        callbacks_.begin_rename.Run();
        return true;
      }
      return false;
    case ui::VKEY_RETURN:
    case ui::VKEY_SPACE:
      callbacks_.select.Run();
      return true;
    case ui::VKEY_DELETE:
      callbacks_.key.Run(aerie_sidebar::ListKey::kDelete);
      return true;
    case ui::VKEY_BACK:
      // "Back to pinned page" from the keyboard (issue #180): Backspace on a
      // pinned row that has left its page. Chromium's Alt+Left would collide
      // with the browser's Back, so it is not used.
      // Only the bare key: Shift+Backspace and the others do nothing (Ctrl,
      // Alt and Command are already refused above).
      if (!event.IsShiftDown() && back_to_pinned_->GetVisible() &&
          callbacks_.back_to_pinned) {
        callbacks_.back_to_pinned.Run();
        return true;
      }
      return false;
    case ui::VKEY_RIGHT:
      callbacks_.key.Run(aerie_sidebar::ListKey::kRight);
      return true;
    case ui::VKEY_LEFT:
      callbacks_.key.Run(aerie_sidebar::ListKey::kLeft);
      return true;
    case ui::VKEY_UP:
      callbacks_.key.Run(aerie_sidebar::ListKey::kUp);
      return true;
    case ui::VKEY_DOWN:
      callbacks_.key.Run(aerie_sidebar::ListKey::kDown);
      return true;
    case ui::VKEY_HOME:
      callbacks_.key.Run(aerie_sidebar::ListKey::kHome);
      return true;
    case ui::VKEY_END:
      callbacks_.key.Run(aerie_sidebar::ListKey::kEnd);
      return true;
    default:
      return false;
  }
}

bool AerieSidebarRowView::SkipDefaultKeyEventProcessing(
    const ui::KeyEvent& event) {
  // Ctrl+W is the browser's "close tab" accelerator, which closes the selected
  // tab. On a focused row it closes the row's own tab, so the row sees the key
  // first.
  // Alt+Left is the browser's Back; on a row that has a move callback it
  // outdents the row.
  return IsCloseShortcut(event) ||
         (IsMoveShortcut(event) && callbacks_.move) ||
         views::View::SkipDefaultKeyEventProcessing(event);
}

void AerieSidebarRowView::OnFocus() {
  views::View::OnFocus();
  UpdateState();
  if (callbacks_.focused) {
    callbacks_.focused.Run();
  }
}

void AerieSidebarRowView::OnBlur() {
  views::View::OnBlur();
  UpdateState();
}

void AerieSidebarRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateState();
}

void AerieSidebarRowView::UpdateState() {
  UpdateTrailing();
  // The content draws the background, so that it scales and fades with it.
  if (selected_) {
    content_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_selected, aerie::tokens::kRadiusMd));
  } else if (hovered_) {
    content_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_hover, aerie::tokens::kRadiusMd));
  } else {
    content_->SetBackground(nullptr);
  }
  content_->SchedulePaint();
}


bool AerieSidebarRowView::height_animating() const {
  return height_animation_ && height_animation_->is_animating();
}

void AerieSidebarRowView::AnimateHeight(bool grow,
                                        int duration_ms,
                                        gfx::Tween::Type tween) {
  // The row clips while it is shorter than its content. The layer exists only
  // for the length of the motion.
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetMasksToBounds(true);
  if (!height_animation_) {
    height_animation_ = std::make_unique<gfx::SlideAnimation>(this);
  }
  height_animation_->SetTweenType(tween);
  height_animation_->SetSlideDuration(base::Milliseconds(duration_ms));
  height_animation_->Reset(grow ? 0.0 : 1.0);
  height_fraction_ = grow ? 0.0 : 1.0;
  if (grow) {
    height_animation_->Show();
  } else {
    height_animation_->Hide();
  }
  PreferredSizeChanged();
}

void AerieSidebarRowView::FinishHeightAnimation() {
  if (layer() && !drag_layers_) {
    DestroyLayer();
  }
}

void AerieSidebarRowView::AnimationProgressed(const gfx::Animation* animation) {
  height_fraction_ = height_animation_->GetCurrentValue();
  PreferredSizeChanged();
}

void AerieSidebarRowView::AnimationEnded(const gfx::Animation* animation) {
  height_fraction_ = height_animation_->GetCurrentValue();
  PreferredSizeChanged();
  if (closing_) {
    if (closed_) {
      std::move(closed_).Run();
    }
    return;
  }
  FinishHeightAnimation();
}

gfx::Transform AerieSidebarRowView::LiftTransform(float scale) const {
  return ScaleAboutCenter(gfx::Size(width(), row_height_), scale);
}

void AerieSidebarRowView::EnsureDragLayers() {
  settle_timer_.Stop();
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  drag_layers_ = true;
}

void AerieSidebarRowView::DropDragLayers() {
  settle_timer_.Stop();
  drag_layers_ = false;
  // A layer that the height animation needs stays; it drops its own.
  if (layer() && !height_animating()) {
    DestroyLayer();
  }
}

void AerieSidebarRowView::BeginLift(const PrefService* prefs) {
  EnsureDragLayers();
  row_motion_.Abort();
  lift_motion_.Abort();
  // The lifted row is above the rows it passes.
  if (layer()->parent()) {
    layer()->parent()->StackAtTop(layer());
  }
  const aerie_sidebar::DragMotion motion =
      aerie_sidebar::RowDragMotion(aerie::motion::ShouldReduceMotion(prefs));
  if (motion.animate_lift) {
    aerie::motion::LayerMotion target;
    target.transform = LiftTransform(motion.lift_scale);
    lift_motion_ =
        aerie::motion::AnimateLayer(content_->layer(), target,
                                    motion.lift_duration, motion.lift_easing,
                                    prefs);
  }
}

void AerieSidebarRowView::SetDragOffset(int dy) {
  EnsureDragLayers();
  row_motion_.Abort();
  drag_offset_ = dy;
  layer()->SetTransform(gfx::Transform::MakeTranslation(0, dy));
}

void AerieSidebarRowView::ShiftTo(int dy, const PrefService* prefs) {
  EnsureDragLayers();
  row_motion_.Abort();
  drag_offset_ = dy;
  const aerie_sidebar::DragMotion motion =
      aerie_sidebar::RowDragMotion(aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion target;
  target.transform = gfx::Transform::MakeTranslation(0, dy);
  // Reduced motion sets the transform at once: AnimateLayer never animates it.
  row_motion_ = aerie::motion::AnimateLayer(layer(), target,
                                            motion.shift_duration,
                                            motion.shift_easing, prefs);
  if (dy == 0) {
    // A row back at its slot gives its layer up when the motion has ended.
    settle_timer_.Start(
        FROM_HERE,
        base::Milliseconds(
            aerie::tokens::GetDurationMs(motion.shift_duration, false) +
            kDropLayersSlackMs),
        base::BindOnce(&AerieSidebarRowView::DropDragLayers,
                       base::Unretained(this)));
  }
}

void AerieSidebarRowView::Settle(int from_dy, const PrefService* prefs) {
  EnsureDragLayers();
  row_motion_.Abort();
  lift_motion_.Abort();
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::DragMotion motion = aerie_sidebar::RowDragMotion(reduced);
  layer()->SetTransform(gfx::Transform::MakeTranslation(0, from_dy));
  drag_offset_ = 0;
  aerie::motion::LayerMotion rest;
  rest.transform = gfx::Transform();
  row_motion_ = aerie::motion::AnimateLayerForMs(
      layer(), rest, motion.settle_duration_ms,
      motion.settle_reduced_duration_ms, motion.settle_easing, prefs);
  lift_motion_ = aerie::motion::AnimateLayer(content_->layer(), rest,
                                             motion.lift_duration,
                                             motion.lift_easing, prefs);
  if (!motion.animate_settle) {
    // The drop is instant; nothing is left to wait for.
    DropDragLayers();
    return;
  }
  settle_timer_.Start(
      FROM_HERE,
      base::Milliseconds(motion.settle_duration_ms + kDropLayersSlackMs),
      base::BindOnce(&AerieSidebarRowView::DropDragLayers,
                     base::Unretained(this)));
}

void AerieSidebarRowView::ResetDrag() {
  row_motion_.Abort();
  lift_motion_.Abort();
  drag_offset_ = 0;
  if (layer()) {
    layer()->SetTransform(gfx::Transform());
  }
  if (content_->layer() && drag_layers_) {
    content_->layer()->SetTransform(gfx::Transform());
  }
  DropDragLayers();
}

void AerieSidebarRowView::PlayOpen(const PrefService* prefs) {
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::RowMotion motion = aerie_sidebar::OpenRowMotion(reduced);
  content_motion_.Abort();
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  ui::Layer* layer = content_->layer();
  aerie::motion::LayerMotion target;
  layer->SetOpacity(motion.from_opacity);
  target.opacity = motion.to_opacity;
  if (motion.animate_scale) {
    // The row has not been laid out yet: it will be as wide as its list.
    const int row_width = parent() ? parent()->width() : width();
    layer->SetTransform(ScaleAboutCenter(gfx::Size(row_width, row_height_),
                                         motion.from_scale));
    target.transform = gfx::Transform();
  }
  content_motion_ = aerie::motion::AnimateLayer(
      layer, target, motion.content_duration, motion.content_easing, prefs);
  if (motion.animate_height) {
    AnimateHeight(/*grow=*/true, motion.height_duration_ms, kHeightTween);
  }
}

void AerieSidebarRowView::PlayClose(const PrefService* prefs,
                                    int outer_edge_sign,
                                    base::OnceClosure done) {
  if (closing_) {
    return;
  }
  // A field that is open when the tab goes away is dropped.
  CancelRename();
  closing_ = true;
  closed_ = std::move(done);
  SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  GetViewAccessibility().SetIsIgnored(true);
  UpdateState();

  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::RowMotion motion =
      aerie_sidebar::CloseRowMotion(outer_edge_sign, reduced);
  content_motion_.Abort();
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  if (motion.animate_slide) {
    target.transform = gfx::Transform::MakeTranslation(motion.slide_x, 0);
  }
  content_motion_ = aerie::motion::AnimateLayer(
      content_->layer(), target, motion.content_duration,
      motion.content_easing, prefs);

  // The height collapses once the content has gone. Under reduced motion it
  // snaps: the row is done when its content has faded.
  close_collapses_ = motion.animate_height;
  close_height_ms_ = motion.height_duration_ms;
  collapse_timer_.Start(
      FROM_HERE, base::Milliseconds(motion.content_duration_ms),
      base::BindOnce(&AerieSidebarRowView::CollapseAfterContent,
                     base::Unretained(this)));
}

void AerieSidebarRowView::CollapseAfterContent() {
  if (close_collapses_) {
    AnimateHeight(/*grow=*/false, close_height_ms_, kHeightTween);
    // A platform without rich animation ends the animation at once.
    return;
  }
  if (closed_) {
    std::move(closed_).Run();
  }
}

void AerieSidebarRowView::SetLevel(int level) {
  if (level_ == level) {
    return;
  }
  level_ = level;
  UpdateIndent();
}

void AerieSidebarRowView::UpdateIndent() {
  content_layout_->set_inside_border_insets(gfx::Insets::TLBR(
      0, aerie::tokens::kSpace8 + level_ * aerie::tokens::kSizeIndent, 0,
      aerie::tokens::kSpace8));
  content_->InvalidateLayout();
  GetViewAccessibility().SetHierarchicalLevel(level_ + 1);
}

void AerieSidebarRowView::PlayFolderFade(
    const aerie_sidebar::FolderMotion& motion,
    int delay_ms,
    const PrefService* prefs,
    bool start_from_current) {
  fade_timer_.Stop();
  content_motion_.Abort();
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  if (!start_from_current) {
    content_->layer()->SetOpacity(motion.from_opacity);
  }
  auto start = [](base::WeakPtr<AerieSidebarRowView> row,
                  aerie_sidebar::FolderMotion motion, const PrefService* prefs) {
    if (!row || !row->content_->layer()) {
      return;
    }
    aerie::motion::LayerMotion target;
    target.opacity = motion.to_opacity;
    row->content_motion_ = aerie::motion::AnimateLayer(
        row->content_->layer(), target, motion.duration, motion.easing, prefs);
  };
  if (delay_ms <= 0) {
    start(weak_factory_.GetWeakPtr(), motion, prefs);
    return;
  }
  fade_timer_.Start(FROM_HERE, base::Milliseconds(delay_ms),
                    base::BindOnce(start, weak_factory_.GetWeakPtr(), motion,
                                   prefs));
}

bool AerieSidebarRowView::folder_fading() const {
  return fade_timer_.IsRunning() || content_motion_.IsAnimating();
}

void AerieSidebarRowView::SetHeightFraction(double fraction) {
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetMasksToBounds(true);
  height_fraction_ = fraction;
  PreferredSizeChanged();
}

void AerieSidebarRowView::EndFolderMotion() {
  fade_timer_.Stop();
  content_motion_.Abort();
  height_fraction_ = 1.0;
  if (content_->layer() && !drag_layers_) {
    content_->DestroyLayer();
  }
  if (layer() && !drag_layers_ && !height_animating()) {
    DestroyLayer();
  }
  PreferredSizeChanged();
}

BEGIN_METADATA(AerieSidebarRowView)
END_METADATA
