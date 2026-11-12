// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_FIELD_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_FIELD_H_

#include <optional>

#include "base/functional/callback.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield.h"

// The text field in which a tab is renamed (issue #145): in place of a row's
// title, and in the Favorites rename popover. It keeps Enter, Esc and F2 away
// from the browser's accelerators, so that its controller sees them first, and
// it tells its owner when it loses keyboard focus. With a palette its colours
// and border are the sidebar's (set again after every theme change); without
// one it keeps the theme's own, as in the popover.
class AerieSidebarRenameField : public views::Textfield {
  METADATA_HEADER(AerieSidebarRenameField, views::Textfield)

 public:
  explicit AerieSidebarRenameField(base::RepeatingClosure on_blur);
  AerieSidebarRenameField(const AerieSidebarRenameField&) = delete;
  AerieSidebarRenameField& operator=(const AerieSidebarRenameField&) = delete;
  ~AerieSidebarRenameField() override;

  void SetPaletteColors(const aerie_sidebar::SidebarPalette& palette);

  // views::Textfield:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  void OnBlur() override;
  void OnThemeChanged() override;

 private:
  void ApplyColors();

  base::RepeatingClosure on_blur_;
  std::optional<aerie_sidebar::SidebarPalette> palette_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_FIELD_H_
