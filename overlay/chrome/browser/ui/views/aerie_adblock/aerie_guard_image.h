// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_IMAGE_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_IMAGE_H_

#include <string>

#include "ui/base/models/image_model.h"

namespace aerie_guard {

// The image of the Aerie Guard icon in the address bar (issue #99): the shield
// for "Guard on" or, when |blocking_off| is true, the shield for "Guard off for
// this site", with |badge_label| drawn into it at the bottom right when it is
// not empty. The text next to a page action is hidden by Helium
// (location-bar-page-action.patch), so the count has to be in the image. The
// colors come from the theme when the image is drawn.
ui::ImageModel GuardImage(bool blocking_off, const std::u16string& badge_label);

}  // namespace aerie_guard

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_IMAGE_H_
