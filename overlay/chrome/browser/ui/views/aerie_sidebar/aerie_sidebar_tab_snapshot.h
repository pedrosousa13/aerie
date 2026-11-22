// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TAB_SNAPSHOT_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TAB_SNAPSHOT_H_

#include <memory>

#include "base/functional/callback.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Canvas;
}

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
}

// The snapshots of tabs that the sidebar's cards show: the Ctrl+Tab recent tabs
// overlay (issue #269) and the tab preview card (issue #270) share this one
// path, so that the privacy rules are in one place.
//
// A snapshot is the thumbnail that Chromium keeps for its own tab hover cards
// (ThumbnailTabHelper). It lives in memory only; nothing here writes one, and
// Aerie takes none of its own. A private tab never has one, neither asked for
// nor shown.
namespace aerie_sidebar {

// True for a tab that must never have a snapshot taken or shown: a tab of an
// incognito window, and later a tab of a Private Workspace (issue #259), whose
// check goes in this one function.
bool IsPrivateTab(tabs::TabInterface* tab);

using SnapshotCallback = base::RepeatingCallback<void(gfx::ImageSkia)>;

// Watches the snapshot that Chromium already has for `tab`, decoded to about
// `size_hint`, and calls `callback` with it (now, or when it is decoded and
// whenever it changes) for as long as the returned subscription lives. Null,
// and `callback` is never called, for a private tab and for a tab that has no
// snapshot yet: a card never makes a tab take a picture it did not have.
std::unique_ptr<ThumbnailImage::Subscription> WatchTabSnapshot(
    tabs::TabInterface* tab,
    const gfx::Size& size_hint,
    SnapshotCallback callback);

// The page's own icon for a card; for a page that has none (the favicon driver
// then hands out a dull grey default), the globe in `fallback_color`, which the
// caller takes from the popover palette so that it holds its contrast.
ui::ImageModel TabCardFavicon(content::WebContents* contents,
                              SkColor fallback_color);

// Draws `snapshot` to fill `area` with rounded corners of `radius`, cropped at
// the bottom and the sides to keep its proportions.
void PaintSnapshot(gfx::Canvas* canvas,
                   const gfx::ImageSkia& snapshot,
                   const gfx::Rect& area,
                   float radius);

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TAB_SNAPSHOT_H_
