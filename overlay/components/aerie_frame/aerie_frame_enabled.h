// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FRAME_AERIE_FRAME_ENABLED_H_
#define COMPONENTS_AERIE_FRAME_AERIE_FRAME_ENABLED_H_

#include "third_party/skia/include/core/SkColor.h"

class PrefService;

namespace aerie_frame {

// True when the window frame is painted by the Aerie tint (issue #178): a
// normal browser window (not a popup or an app window) while the layout is the
// sidebar. Then Helium's frame fills stay transparent so the tint shows, native
// frame materials are off (nothing is mixed with the OS material), the page
// card is rounded whatever the Helium pref says, and the card radius is
// `radius-xl`.
bool IsFrameTintActive(const PrefService& prefs, bool is_normal_window);

// The page card's radius, `radius-xl`.
int CardRadius();

}  // namespace aerie_frame

#endif  // COMPONENTS_AERIE_FRAME_AERIE_FRAME_ENABLED_H_
