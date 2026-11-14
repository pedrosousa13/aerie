// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FLOATING_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FLOATING_H_

#include "ui/base/class_properties.h"
#include "ui/base/class_property.h"

namespace aerie_sidebar {

// A property of the sidebar's region view (issue #184): the width in px that the
// peeked, auto-hidden sidebar floats at over the page, or 0 when it is not
// floating. The window layout reads it and puts the region at that width at the
// window edge without giving the page less room; the sidebar view sets it. The
// property sits between the two because the layout (in //chrome/browser/ui/
// views/frame) cannot depend on the sidebar view.
extern const ui::ClassProperty<int>* const kFloatingWidth;

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FLOATING_H_
