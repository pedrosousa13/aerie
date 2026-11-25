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

// Where files may be read from. Both lists hold directories with symlinks resolved.
//  * contain: a file is read only if its resolved path lies inside one of these: every data dir,
//    and for a Flatpak installation's export directory (<install>/flatpak/exports/share, with
//    <install>/flatpak/app present) also <install>/flatpak/app, because the exported entries and
//    icons are symlinks into app/<id>/....
//  * bases: the directory a file is opened beneath (see ReadRegularFile in browser_icon.cc).
struct ReadRoots {
  std::vector<base::FilePath> contain;
  std::vector<base::FilePath> bases;
};

// Looks icons up in `data_dirs`. It resolves the data dirs and lists the icon themes once, so
// looking up several browsers does not repeat that work.
class IconResolver {
 public:
  explicit IconResolver(std::vector<base::FilePath> data_dirs);
  ~IconResolver();

  // The icon of the first of `desktop_ids` that has a .desktop entry in the data dirs and whose
  // Icon= resolves to a PNG file there (hicolor first, then the other themes, then pixmaps/;
  // sizes near 64 px first), as a "data:image/png;base64,..." URL; empty when nothing
  // qualifies: no entry, SVG or XPM only, not a PNG by its first bytes, larger than
  // kMaxIconBytes, or a file that is not a regular file inside the allowed places (an absolute
  // Icon= path or a symlink that leaves the data dirs, such as Debian's /usr/lib/firefox, gets
  // no icon and the page keeps its letter tile). It only reads files and never touches the
  // network.
  std::string DataUrlFor(const std::vector<std::string>& desktop_ids) const;

 private:
  std::vector<base::FilePath> data_dirs_;
  ReadRoots roots_;
  std::vector<std::vector<std::string>> themes_;  // Per data dir, hicolor first.
};

// IconResolver(data_dirs).DataUrlFor(desktop_ids).
std::string BrowserIconDataUrl(const std::vector<base::FilePath>& data_dirs,
                               const std::vector<std::string>& desktop_ids);

// Whether files are opened with openat2() on this build and kernel (see ReadRegularFile in
// browser_icon.cc); false means the O_NOFOLLOW fallback is used.
bool Openat2Usable();

// Turns the openat2() path off, so a test can exercise the fallback.
void SetOpenat2DisabledForTesting(bool disabled);

// The .desktop file ids (without ".desktop") to look for the icon of the browser that
// the import step lists as `name`, such as "Chromium" or "Zen (Flatpak)". A name that
// ends in " (Flatpak)" gets Flatpak ids only, any other name gets the native ids. An
// unknown name gets none.
std::vector<std::string> DesktopIdsForBrowser(std::string_view name);

}  // namespace aerie_import_sources

#endif  // COMPONENTS_AERIE_IMPORT_SOURCES_BROWSER_ICON_H_
