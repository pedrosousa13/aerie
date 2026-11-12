// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_popover.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_fade.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_field.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

// static
AerieSidebarRenamePopover* AerieSidebarRenamePopover::Show(
    views::View* anchor,
    const std::u16string& initial_text,
    const PrefService* reduced_motion_prefs,
    DoneCallback done) {
  auto popover = base::WrapUnique(
      new AerieSidebarRenamePopover(anchor, initial_text, std::move(done)));
  AerieSidebarRenamePopover* raw = popover.get();
  views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(popover), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
      ->Show();
  // The field fades in on `xfast`: opacity only (issue #145).
  raw->field_motion_ = FadeRenameViewIn(raw->field_, reduced_motion_prefs);
  raw->field_->RequestFocus();
  raw->field_->SelectAll(false);
  return raw;
}

AerieSidebarRenamePopover::AerieSidebarRenamePopover(
    views::View* anchor,
    const std::u16string& initial_text,
    DoneCallback done)
    : views::BubbleDialogDelegate(anchor,
                                  views::BubbleBorder::LEFT_TOP,
                                  views::BubbleBorder::DIALOG_SHADOW),
      done_(std::move(done)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  SetAccessibleTitle(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_POPOVER));
  set_fixed_width(aerie::tokens::kSizeSidebarWidth -
                  2 * aerie::tokens::kSpace16);
  set_margins(gfx::Insets(aerie::tokens::kSpace12));
  auto contents = std::make_unique<views::BoxLayoutView>();
  contents->SetOrientation(views::BoxLayout::Orientation::kVertical);
  contents->SetBetweenChildSpacing(aerie::tokens::kSpace4);
  contents->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  field_ = contents->AddChildView(
      std::make_unique<AerieSidebarRenameField>(base::RepeatingClosure()));
  field_->set_controller(this);
  field_->SetText(initial_text);
  field_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_FIELD_NAME));
  field_->GetViewAccessibility().SetDescription(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_CAPTION));
  caption_ = contents->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_CAPTION),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  caption_->SetFontList(caption_->font_list().DeriveWithSizeDelta(
      aerie::tokens::kTypeSizeCaption - caption_->font_list().GetFontSize()));
  caption_->SetMultiLine(true);
  caption_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  caption_->GetViewAccessibility().SetIsIgnored(true);
  SetContentsView(std::move(contents));

  SetInitiallyFocusedView(field_);
  RegisterWindowClosingCallback(base::BindOnce(
      &AerieSidebarRenamePopover::OnClosing, base::Unretained(this)));
}

AerieSidebarRenamePopover::~AerieSidebarRenamePopover() {
  field_motion_.Abort();
}

void AerieSidebarRenamePopover::CancelRename() {
  Finish(/*commit=*/false, /*refocus=*/false);
}

void AerieSidebarRenamePopover::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (new_contents.size() > aerie_tab_model::kMaxManualNameLength) {
    sender->SetText(aerie_tab_model::CutToManualNameLength(new_contents));
  }
}

bool AerieSidebarRenamePopover::HandleKeyEvent(views::Textfield* sender,
                                               const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    Finish(/*commit=*/true, /*refocus=*/true);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Finish(/*commit=*/false, /*refocus=*/true);
    return true;
  }
  return false;
}

void AerieSidebarRenamePopover::Finish(bool commit, bool refocus) {
  if (finished_) {
    return;
  }
  finished_ = true;
  std::optional<std::u16string> text;
  if (commit) {
    text = std::u16string(field_->GetText());
  }
  DoneCallback done = std::move(done_);
  views::Widget* widget = GetWidget();
  if (widget && !widget->IsClosed()) {
    widget->Close();
  }
  if (done) {
    std::move(done).Run(std::move(text), refocus);
  }
}

void AerieSidebarRenamePopover::OnClosing() {
  // Closed by a click elsewhere (the bubble closes when it loses activation):
  // focus loss keeps the name, as in a row.
  if (!finished_) {
    Finish(/*commit=*/true, /*refocus=*/false);
  }
}
