// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_field.h"

#include <utility>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/border.h"

AerieSidebarRenameField::AerieSidebarRenameField(base::RepeatingClosure on_blur)
    : on_blur_(std::move(on_blur)) {}

AerieSidebarRenameField::~AerieSidebarRenameField() = default;

void AerieSidebarRenameField::SetPaletteColors(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  SetCornerRadius(aerie::tokens::kRadiusSm);
  SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(aerie::tokens::kSizeBorderHairlineWidth,
                                     aerie::tokens::kRadiusSm, palette.accent),
      gfx::Insets::VH(aerie::tokens::kSpace2, aerie::tokens::kSpace6)));
  ApplyColors();
}

void AerieSidebarRenameField::ApplyColors() {
  if (!palette_) {
    return;
  }
  SetColor(palette_->text);
  SetBackgroundColor(palette_->row_selected);
}

bool AerieSidebarRenameField::SkipDefaultKeyEventProcessing(
    const ui::KeyEvent& event) {
  switch (event.key_code()) {
    case ui::VKEY_RETURN:
    case ui::VKEY_ESCAPE:
    case ui::VKEY_F2:
      return true;
    default:
      return views::Textfield::SkipDefaultKeyEventProcessing(event);
  }
}

void AerieSidebarRenameField::OnBlur() {
  views::Textfield::OnBlur();
  if (on_blur_) {
    on_blur_.Run();
  }
}

void AerieSidebarRenameField::OnThemeChanged() {
  views::Textfield::OnThemeChanged();
  ApplyColors();
}

BEGIN_METADATA(AerieSidebarRenameField)
END_METADATA
