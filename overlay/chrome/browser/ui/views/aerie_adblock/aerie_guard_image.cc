// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_image.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/aerie_adblock/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"

namespace aerie_guard {

namespace {

constexpr int kBadgeFontSize = 8;
constexpr int kBadgeHeight = 9;
constexpr int kBadgePaddingX = 2;
// "Guard off" is drawn dimmer than "Guard on".
constexpr uint8_t kOffAlpha = 0xA0;

class GuardImageSource : public gfx::CanvasImageSource {
 public:
  GuardImageSource(bool blocking_off,
                   std::u16string badge_label,
                   SkColor icon_color,
                   SkColor badge_background,
                   SkColor badge_foreground)
      : gfx::CanvasImageSource(gfx::Size(kGuardImageSize, kGuardImageSize)),
        blocking_off_(blocking_off),
        badge_label_(std::move(badge_label)),
        icon_color_(icon_color),
        badge_background_(badge_background),
        badge_foreground_(badge_foreground) {}

  void Draw(gfx::Canvas* canvas) override {
    gfx::PaintVectorIcon(
        canvas, blocking_off_ ? kAerieGuardOffIcon : kAerieGuardOnIcon,
        kGuardImageSize,
        blocking_off_ ? SkColorSetA(icon_color_, kOffAlpha) : icon_color_);
    if (badge_label_.empty()) {
      return;
    }
    const gfx::FontList font_list =
        gfx::FontList(gfx::Font(std::string(), kBadgeFontSize))
            .DeriveWithWeight(gfx::Font::Weight::BOLD);
    const int width = std::min(
        kGuardImageSize, gfx::GetStringWidth(badge_label_, font_list) +
                        2 * kBadgePaddingX);
    const gfx::Rect badge(kGuardImageSize - width, kGuardImageSize - kBadgeHeight, width,
                          kBadgeHeight);
    canvas->FillRect(badge, badge_background_);
    canvas->DrawStringRectWithFlags(
        badge_label_, font_list, badge_foreground_, badge,
        gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

 private:
  const bool blocking_off_;
  const std::u16string badge_label_;
  const SkColor icon_color_;
  const SkColor badge_background_;
  const SkColor badge_foreground_;
};

gfx::ImageSkia Generate(bool blocking_off,
                        std::u16string badge_label,
                        const ui::ColorProvider* color_provider) {
  return gfx::CanvasImageSource::MakeImageSkia<GuardImageSource>(
      blocking_off, std::move(badge_label),
      color_provider->GetColor(kColorOmniboxIconForeground),
      color_provider->GetColor(ui::kColorButtonBackgroundProminent),
      color_provider->GetColor(ui::kColorButtonForegroundProminent));
}

}  // namespace

ui::ImageModel GuardImage(bool blocking_off,
                          const std::u16string& badge_label) {
  return ui::ImageModel::FromImageGenerator(
      base::BindRepeating(&Generate, blocking_off, badge_label),
      gfx::Size(kGuardImageSize, kGuardImageSize));
}

}  // namespace aerie_guard
