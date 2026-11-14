// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DENSITY_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DENSITY_H_

#include <string>

class PrefService;

// The density of the sidebar and the side it sits on (issue #184,
// docs/sidebar-design.md, "Densities, side and auto-hide"). Headless, so it has
// unit tests; the sizes come from the generated tokens.
namespace aerie_sidebar {

enum class Density {
  kComfortable,
  kCompact,
  // The thin strip of icons (`size-rail-width`).
  kRail,
  // Hidden at rest and floating over the page when peeking. It lays out its
  // rows as Comfortable (decision of issue #184, docs/sidebar-design.md).
  kAutoHide,
};

// One table of sizes per density. All in px.
struct DensitySizes {
  int row_height;
  // The side of a Favorites tile.
  int tile;
  int favicon;
  // The icon of a row of the lists (the tile uses `favicon`). Rows draw 16 px
  // (`size-icon`) in Comfortable, so Compact takes the same step down as the
  // tile's favicon does (decision of issue #184).
  int row_icon;
  // The room between the sidebar's edge and its rows.
  int inset;
  // True in the rail: titles, trailing items, section labels and the resizer
  // are hidden, and the address pill becomes a square Guard button.
  bool icons_only;
  // 0 for the width the user chose; `size-rail-width` in the rail.
  int fixed_width;
};

// The density the pref names. Anything that is not one of the four names reads
// as Comfortable.
Density ParseDensity(const std::string& value);
const char* DensityName(Density density);
Density GetDensity(const PrefService& prefs);

DensitySizes SizesFor(Density density);

// True when the sidebar is out of the layout at rest.
bool IsAutoHide(Density density);

// Which side the sidebar is on. Anything but "right" is the left.
bool IsOnRight(const PrefService& prefs);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DENSITY_H_
