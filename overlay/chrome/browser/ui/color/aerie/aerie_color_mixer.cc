// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/color/aerie/aerie_color_mixer.h"

#include <initializer_list>
#include <optional>

#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/aerie_frame/aerie_frame_look.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"

namespace aerie_color {

namespace {

// A provider can be built on a thread other than the one that changes the
// look, so the state has a lock. Reads copy it.
base::Lock& StateLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

std::optional<ActiveLook>& State() {
  static base::NoDestructor<std::optional<ActiveLook>> state;
  return *state;
}

// Gives every role in `ids` the colour.
void Set(ui::ColorMixer& mixer,
         std::initializer_list<ui::ColorId> ids,
         SkColor color) {
  for (ui::ColorId id : ids) {
    mixer[id] = {color};
  }
}

}  // namespace

bool SetActiveLook(aerie::tokens::ThemeId theme, std::string_view colour) {
  base::AutoLock lock(StateLock());
  std::optional<ActiveLook>& state = State();
  if (state && state->theme == theme && state->colour == colour) {
    return false;
  }
  state = ActiveLook{theme, std::string(colour)};
  return true;
}

void ResetActiveLookForTesting() {
  base::AutoLock lock(StateLock());
  State().reset();
}

bool HasActiveLook() {
  base::AutoLock lock(StateLock());
  return State().has_value();
}

ActiveLook GetActiveLook() {
  base::AutoLock lock(StateLock());
  return State().value_or(ActiveLook{});
}

void AddAerieColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key) {
  if (key.forced_colors != ui::ColorProviderKey::ForcedColors::kNone) {
    return;
  }
  std::optional<ActiveLook> look;
  {
    base::AutoLock lock(StateLock());
    look = State();
  }
  if (!look) {
    return;
  }
  const aerie::tokens::ColorMode mode =
      key.color_mode == ui::ColorProviderKey::ColorMode::kDark
          ? aerie::tokens::ColorMode::kDark
          : aerie::tokens::ColorMode::kLight;
  const aerie_frame::ChromeColors c =
      aerie_frame::ResolveChromeColors(look->theme, mode, look->colour);

  ui::ColorMixer& mixer = provider->AddMixer();
  // Surface.
  Set(mixer,
      {ui::kColorMenuBackground, ui::kColorBubbleBackground,
       ui::kColorDialogBackground, ui::kColorPrimaryBackground,
       kColorOmniboxResultsBackground, ui::kColorSysSurface,
       ui::kColorSysSurfaceContainer, ui::kColorSysSurfaceContainerLow},
      c.surface);
  // Text.
  Set(mixer,
      {ui::kColorPrimaryForeground, ui::kColorMenuItemForeground,
       ui::kColorDialogForeground, ui::kColorLabelForeground, kColorOmniboxText,
       ui::kColorSysOnSurface},
      c.text);
  // Text muted.
  Set(mixer,
      {ui::kColorSecondaryForeground, kColorOmniboxTextDimmed,
       ui::kColorSysOnSurfaceVariant, ui::kColorSysOnSurfaceSubtle,
       ui::kColorSysOnSurfaceSecondary},
      c.text_muted);
  // Accent.
  Set(mixer,
      {ui::kColorAccent, ui::kColorSysPrimary, ui::kColorSysPrimaryContainer},
      c.accent);
  // Text and icons on the accent (prominent buttons, tonal fills). Chromium
  // derives these from its own primary, which the roles above no longer are.
  Set(mixer,
      {ui::kColorSysOnPrimary, ui::kColorSysOnPrimaryContainer,
       ui::kColorButtonForegroundProminent},
      c.on_accent);
  // Focus ring.
  Set(mixer, {ui::kColorFocusableBorderFocused, ui::kColorSysStateFocusRing},
      c.focus_ring);
  // Divider and hairline.
  Set(mixer,
      {ui::kColorSeparator, ui::kColorMenuSeparator, ui::kColorSysDivider},
      c.divider);
  Set(mixer, {ui::kColorMenuBorder, ui::kColorBubbleBorder}, c.hairline);

  // The horizontal tab strip and toolbar (issue #186). The selected tab and
  // the toolbar are the page card's surface, so the tab joins the toolbar at
  // the top of the card; the other tabs are clear over the frame.
  Set(mixer, {ui::kColorFrameActive, ui::kColorFrameInactive}, c.frame);
  Set(mixer,
      {kColorTabBackgroundActiveFrameActive,
       kColorTabBackgroundActiveFrameInactive, kColorToolbar},
      c.card_surface);
  Set(mixer,
      {kColorTabBackgroundInactiveFrameActive,
       kColorTabBackgroundInactiveFrameInactive},
      SK_ColorTRANSPARENT);
  Set(mixer,
      {kColorTabBackgroundInactiveHoverFrameActive,
       kColorTabBackgroundInactiveHoverFrameInactive},
      c.row_hover);
  Set(mixer,
      {kColorTabForegroundActiveFrameActive,
       kColorTabForegroundActiveFrameInactive,
       kColorTabForegroundInactiveFrameActive,
       kColorTabForegroundInactiveFrameInactive},
      c.text);
  Set(mixer, {kColorTabDividerFrameActive, kColorTabDividerFrameInactive},
      c.divider);
  Set(mixer, {kColorTabStripScrollButtonIcon, kColorToolbarButtonIcon},
      c.text_muted);
}

}  // namespace aerie_color
