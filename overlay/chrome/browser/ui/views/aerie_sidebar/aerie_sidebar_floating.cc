// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_floating.h"

#include "ui/base/class_properties.h"
#include "ui/views/view.h"

namespace aerie_sidebar {

DEFINE_UI_CLASS_PROPERTY_KEY(int, kFloatingWidth, 0)

void SetFloatingWidth(views::View* region, int width) {
  if (!region || region->GetProperty(kFloatingWidth) == width) {
    return;
  }
  region->SetProperty(kFloatingWidth, width);
  region->InvalidateLayout();
  if (region->parent()) {
    region->parent()->InvalidateLayout();
  }
}

void FloatHidingColumn(views::View* region, HideSlide& slide, int column_width) {
  slide.Start(column_width);
  SetFloatingWidth(region, slide.floating_width());
}

void LandHidingColumn(views::View* region, HideSlide& slide) {
  if (slide.Stop()) {
    SetFloatingWidth(region, slide.floating_width());
  }
}

}  // namespace aerie_sidebar
