// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_density.h"

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"

namespace aerie_sidebar {

Density ParseDensity(const std::string& value) {
  if (value == kDensityCompact) {
    return Density::kCompact;
  }
  if (value == kDensityRail) {
    return Density::kRail;
  }
  if (value == kDensityAutoHide) {
    return Density::kAutoHide;
  }
  return Density::kComfortable;
}

const char* DensityName(Density density) {
  switch (density) {
    case Density::kComfortable:
      return kDensityComfortable;
    case Density::kCompact:
      return kDensityCompact;
    case Density::kRail:
      return kDensityRail;
    case Density::kAutoHide:
      return kDensityAutoHide;
  }
  return kDensityComfortable;
}

Density GetDensity(const PrefService& prefs) {
  return ParseDensity(prefs.GetString(kDensityPref));
}

DensitySizes SizesFor(Density density) {
  namespace t = aerie::tokens;
  switch (density) {
    case Density::kCompact:
      return {t::kSizeRowHeightCompact, t::kSizeTileCompact,
              t::kSizeFaviconCompact,
              t::kSizeIcon + t::kSizeFaviconCompact - t::kSizeFaviconComfortable,
              t::kSizeInsetCompact, false, 0};
    case Density::kRail:
      // The tile is `size-rail-tile` square, in a column `size-rail-width`
      // wide, so the inset is what is left on each side of it.
      return {t::kSizeRailTile, t::kSizeRailTile, t::kSizeFaviconComfortable,
              t::kSizeIcon,
              (t::kSizeRailWidth - t::kSizeRailTile) / 2, true,
              t::kSizeRailWidth};
    case Density::kComfortable:
    case Density::kAutoHide:
      break;
  }
  return {t::kSizeRowHeightComfortable, t::kSizeTileComfortable,
          t::kSizeFaviconComfortable, t::kSizeIcon, t::kSizeInsetComfortable,
          false, 0};
}

bool IsAutoHide(Density density) {
  return density == Density::kAutoHide;
}

bool IsOnRight(const PrefService& prefs) {
  return prefs.GetString(kSidePref) == kSideRight;
}

}  // namespace aerie_sidebar
