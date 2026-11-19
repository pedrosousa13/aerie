// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FLOATING_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FLOATING_H_

#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "ui/base/class_properties.h"
#include "ui/base/class_property.h"

namespace views {
class View;
}

namespace aerie_sidebar {

// A property of the sidebar's region view (issue #184): the width in px that the
// peeked, auto-hidden sidebar floats at over the page, or 0 when it is not
// floating. The window layout reads it and puts the region at that width at the
// window edge without giving the page less room; the sidebar view sets it. The
// property sits between the two because the layout (in //chrome/browser/ui/
// views/frame) cannot depend on the sidebar view.
extern const ui::ClassProperty<int>* const kFloatingWidth;

// Sets the property on `region` (null is ignored) and lays out the region and
// the window layout above it. The parent must lay out too: it is the layout that
// reads the property, so without it the float does not take effect.
void SetFloatingWidth(views::View* region, int width);

// A hide slides the column out (issue #237): the column floats at its own width
// while the page takes its room at once. `LandHidingColumn` ends it; it does
// nothing when no hide is sliding, so it cannot clear a peek's float.
void FloatHidingColumn(views::View* region, HideSlide& slide, int column_width);
void LandHidingColumn(views::View* region, HideSlide& slide);

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FLOATING_H_
