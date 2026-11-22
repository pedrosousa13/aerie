// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tab_snapshot.h"

#include <algorithm>
#include <utility>

#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"

namespace aerie_sidebar {

bool IsPrivateTab(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  // An incognito window. A Private Workspace (issue #259) is a tab of a
  // private storage partition, and must answer true here as well: when it is
  // built, its check goes on this line, the only gate before a snapshot is
  // taken or shown.
  return !contents || contents->GetBrowserContext()->IsOffTheRecord();
}

std::unique_ptr<ThumbnailImage::Subscription> WatchTabSnapshot(
    tabs::TabInterface* tab,
    const gfx::Size& size_hint,
    SnapshotCallback callback) {
  if (!tab || IsPrivateTab(tab)) {
    return nullptr;
  }
  ThumbnailTabHelper* helper = ThumbnailTabHelper::From(tab);
  scoped_refptr<ThumbnailImage> thumbnail =
      helper ? helper->thumbnail() : nullptr;
  if (!thumbnail || !thumbnail->has_data()) {
    return nullptr;
  }
  std::unique_ptr<ThumbnailImage::Subscription> subscription =
      thumbnail->Subscribe();
  subscription->SetSizeHint(size_hint);
  subscription->SetUncompressedImageCallback(std::move(callback));
  thumbnail->RequestThumbnailImage();
  return subscription;
}

ui::ImageModel TabCardFavicon(content::WebContents* contents,
                              SkColor fallback_color) {
  if (auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents)) {
    gfx::Image image = driver->GetFavicon();
    if (driver->FaviconIsValid() && !image.IsEmpty()) {
      return ui::ImageModel::FromImage(image);
    }
  }
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                        fallback_color,
                                        aerie::tokens::kSizeIcon);
}

void PaintSnapshot(gfx::Canvas* canvas,
                   const gfx::ImageSkia& snapshot,
                   const gfx::Rect& area,
                   float radius) {
  canvas->Save();
  canvas->sk_canvas()->clipRRect(
      SkRRect::MakeRectXY(gfx::RectToSkRect(area), radius, radius),
      /*doAntiAlias=*/true);
  const float scale =
      std::max(static_cast<float>(area.width()) / snapshot.width(),
               static_cast<float>(area.height()) / snapshot.height());
  const int source_w = std::min<int>(snapshot.width(), area.width() / scale);
  const int source_h = std::min<int>(snapshot.height(), area.height() / scale);
  const int source_x = (snapshot.width() - source_w) / 2;
  canvas->DrawImageInt(snapshot, source_x, 0, source_w, source_h, area.x(),
                       area.y(), area.width(), area.height(),
                       /*filter=*/true);
  canvas->Restore();
}

}  // namespace aerie_sidebar
