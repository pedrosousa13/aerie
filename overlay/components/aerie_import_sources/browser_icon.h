// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_IMPORT_SOURCES_BROWSER_ICON_H_
#define COMPONENTS_AERIE_IMPORT_SOURCES_BROWSER_ICON_H_

#include <stddef.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace aerie_import_sources {

// The largest icon file that is read. A bigger file is skipped (the next size is
// tried), so a hostile or broken icon cannot make the browser read or send much.
inline constexpr size_t kMaxIconBytes = 256 * 1024;

// The directories, shaped like /usr/share, that hold applications/*.desktop files and
// icons/ and pixmaps/: $XDG_DATA_HOME (default ~/.local/share), $XDG_DATA_DIRS (default
// /usr/local/share:/usr/share), and the Flatpak export directories (user first, then
// /var/lib/flatpak/exports/share). Earlier entries win.
std::vector<base::FilePath> SystemDataDirs();

// Finds the icon of the first of `desktop_ids` that has a .desktop entry in
// `data_dirs` and whose Icon= resolves to a PNG file there (hicolor first, then the
// other themes, then pixmaps/; sizes near 64 px first). Returns it as a
// "data:image/png;base64,..." URL, or an empty string when nothing qualifies: no
// entry, SVG or XPM only, not a PNG by its first bytes, or larger than kMaxIconBytes.
// It only reads files. It never touches the network.
std::string BrowserIconDataUrl(const std::vector<base::FilePath>& data_dirs,
                               const std::vector<std::string>& desktop_ids);

// The .desktop file ids (without ".desktop") to look for the icon of the browser that
// the import step lists as `name`, such as "Chromium" or "Zen (Flatpak)". A name that
// ends in " (Flatpak)" gets Flatpak ids only, any other name gets the native ids. An
// unknown name gets none.
std::vector<std::string> DesktopIdsForBrowser(std::string_view name);

}  // namespace aerie_import_sources

#endif  // COMPONENTS_AERIE_IMPORT_SOURCES_BROWSER_ICON_H_
