// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/browser_icon.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "build/build_config.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/aerie_import_sources/import_sources.h"
#include "components/aerie_import_sources/known_browsers.h"

#if BUILDFLAG(IS_POSIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

namespace aerie_import_sources {

namespace {

// Sizes in the order they are tried: near 64 px first (the row shows 36 px, and
// 2x screens want more), then larger, then smaller.
constexpr std::array<std::string_view, 11> kSizes = {
    "64x64",   "48x48",   "128x128", "96x96",   "72x72",   "256x256",
    "512x512", "32x32",   "24x24",   "22x22",   "16x16"};

constexpr std::string_view kPngMagic("\x89PNG\r\n\x1a\n", 8);
constexpr std::string_view kIhdr("IHDR");

constexpr size_t kMaxDesktopFileBytes = 64 * 1024;

// A file name that stays inside the directory it is joined to.
bool IsPlainName(std::string_view name) {
  if (name.empty() || name.size() > 255 || name == "." || name == "..") {
    return false;
  }
  return name.find_first_of("/\\") == std::string_view::npos &&
         name.find('\0') == std::string_view::npos;
}

// The value of Icon= in the [Desktop Entry] group of `contents`, or "".
std::string IconOfDesktopEntry(const std::string& contents) {
  bool in_entry = false;
  for (std::string_view line :
       base::SplitStringPiece(contents, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (line.front() == '[') {
      in_entry = line == "[Desktop Entry]";
      continue;
    }
    if (in_entry && line.starts_with("Icon=")) {
      return std::string(
          base::TrimWhitespaceASCII(line.substr(5), base::TRIM_ALL));
    }
  }
  return std::string();
}

// The places a desktop entry, an icon file or an absolute Icon= path may really live, with
// symlinks resolved: every data dir, and for a Flatpak export directory
// (<installation>/exports/share) the whole installation, because its entries and icons are
// symlinks into <installation>/app/<id>/....
class AllowedRoots {
 public:
  explicit AllowedRoots(const std::vector<base::FilePath>& data_dirs) {
    for (const base::FilePath& dir : data_dirs) {
      Add(dir);
      if (dir.BaseName().value() == "share" &&
          dir.DirName().BaseName().value() == "exports") {
        Add(dir.DirName().DirName());
      }
    }
  }

  // Whether `real` (already resolved) is inside one of the roots.
  bool Contains(const base::FilePath& real) const {
    return std::any_of(roots_.begin(), roots_.end(),
                       [&](const base::FilePath& root) {
                         return root.IsParent(real);
                       });
  }

 private:
  void Add(const base::FilePath& dir) {
    const base::FilePath real = base::MakeAbsoluteFilePath(dir);
    if (!real.empty()) {
      roots_.push_back(real);
    }
  }

  std::vector<base::FilePath> roots_;
};

// The contents of a regular file of at most `max_bytes`, which must lie inside `roots` once its
// symlinks are resolved. It resolves the path once, opens the result without following a
// symlink at the last step, and checks and reads through that one descriptor, so the file that
// was checked is the file that is read.
std::optional<std::string> ReadRegularFile(const base::FilePath& path,
                                           const AllowedRoots& roots,
                                           size_t max_bytes) {
#if BUILDFLAG(IS_POSIX)
  const base::FilePath real = base::MakeAbsoluteFilePath(path);
  if (real.empty() || !roots.Contains(real)) {
    return std::nullopt;
  }
  // O_NONBLOCK so that a FIFO that slipped in cannot block the open; fstat then refuses it.
  base::ScopedFD fd(HANDLE_EINTR(
      open(real.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  if (!fd.is_valid()) {
    return std::nullopt;
  }
  struct stat info;
  if (fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size <= 0 || static_cast<uint64_t>(info.st_size) > max_bytes) {
    return std::nullopt;
  }
  std::string contents(static_cast<size_t>(info.st_size), '\0');
  base::File file(std::move(fd));
  if (!file.ReadAtCurrentPosAndCheck(base::as_writable_byte_span(contents))) {
    return std::nullopt;
  }
  return contents;
#else
  return std::nullopt;
#endif
}

// The bytes of a PNG file (by its signature and IHDR chunk) read as above.
std::optional<std::string> ReadPng(const base::FilePath& path,
                                   const AllowedRoots& roots) {
  std::optional<std::string> bytes = ReadRegularFile(path, roots, kMaxIconBytes);
  if (!bytes || bytes->size() < 16) {
    return std::nullopt;
  }
  const std::string_view view(*bytes);
  if (!view.starts_with(kPngMagic) || view.substr(12, 4) != kIhdr) {
    return std::nullopt;
  }
  return bytes;
}

// The theme directories under `<data dir>/icons`: hicolor first, then the others by
// name.
std::vector<std::string> ThemeNames(const base::FilePath& icons_dir) {
  std::vector<std::string> others;
  base::FileEnumerator themes(icons_dir, false,
                              base::FileEnumerator::DIRECTORIES);
  for (base::FilePath p = themes.Next(); !p.empty(); p = themes.Next()) {
    const std::string name = p.BaseName().AsUTF8Unsafe();
    if (name != "hicolor" && IsPlainName(name)) {
      others.push_back(name);
    }
  }
  std::sort(others.begin(), others.end());
  std::vector<std::string> result = {"hicolor"};
  result.insert(result.end(), others.begin(), others.end());
  return result;
}

// Every place the icon file `file_name` (a plain name with ".png") may be, best first: the
// hicolor icons of all data dirs by size, then the other themes by size, then pixmaps.
std::vector<base::FilePath> IconCandidates(
    const std::vector<base::FilePath>& data_dirs,
    const std::string& file_name) {
  std::vector<std::vector<std::string>> themes;
  for (const base::FilePath& dir : data_dirs) {
    themes.push_back(ThemeNames(dir.AppendASCII("icons")));
  }
  std::vector<base::FilePath> candidates;
  // Pass 0: hicolor (always the first theme). Pass 1: every other theme.
  for (bool hicolor : {true, false}) {
    for (std::string_view size : kSizes) {
      for (size_t d = 0; d < data_dirs.size(); ++d) {
        for (size_t t = 0; t < themes[d].size(); ++t) {
          if ((t == 0) != hicolor) {
            continue;
          }
          candidates.push_back(data_dirs[d]
                                   .AppendASCII("icons")
                                   .AppendASCII(themes[d][t])
                                   .AppendASCII(size)
                                   .AppendASCII("apps")
                                   .AppendASCII(file_name));
        }
      }
    }
  }
  for (const base::FilePath& dir : data_dirs) {
    candidates.push_back(dir.AppendASCII("pixmaps").AppendASCII(file_name));
  }
  return candidates;
}

// Looks `icon` up in the icon directories of all `data_dirs`. `icon` is an absolute path (which
// must lie inside the data dirs) or a plain icon name.
std::optional<std::string> FindIcon(
    const std::vector<base::FilePath>& data_dirs,
    const AllowedRoots& roots,
    const std::string& icon) {
  if (!icon.empty() && icon.front() == '/') {
    return ReadPng(base::FilePath(icon), roots);
  }
  if (!IsPlainName(icon)) {
    return std::nullopt;
  }
  // The icon name may carry its extension.
  std::string stem = icon;
  if (base::EndsWith(stem, ".png", base::CompareCase::INSENSITIVE_ASCII)) {
    stem.resize(stem.size() - 4);
  }
  for (const base::FilePath& path : IconCandidates(data_dirs, stem + ".png")) {
    if (std::optional<std::string> bytes = ReadPng(path, roots)) {
      return bytes;
    }
  }
  return std::nullopt;
}

}  // namespace

std::vector<base::FilePath> SystemDataDirs() {
  std::vector<base::FilePath> dirs;
  auto env = base::Environment::Create();
  std::string home = env->GetVar("HOME").value_or(std::string());
  auto add = [&dirs](const base::FilePath& dir) {
    if (!dir.empty() && dir.IsAbsolute() &&
        std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) {
      dirs.push_back(dir);
    }
  };

  std::string data_home = env->GetVar("XDG_DATA_HOME").value_or(std::string());
  if (!data_home.empty()) {
    add(base::FilePath(data_home));
  } else if (!home.empty()) {
    add(base::FilePath(home).Append(".local/share"));
  }
  std::string data_dirs = env->GetVar("XDG_DATA_DIRS").value_or(std::string());
  if (data_dirs.empty()) {
    data_dirs = "/usr/local/share:/usr/share";
  }
  for (std::string_view dir : base::SplitStringPiece(
           data_dirs, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    add(base::FilePath(std::string(dir)));
  }
  if (!home.empty()) {
    add(base::FilePath(home).Append(".local/share/flatpak/exports/share"));
  }
  add(base::FilePath("/var/lib/flatpak/exports/share"));
  return dirs;
}

std::string BrowserIconDataUrl(const std::vector<base::FilePath>& data_dirs,
                               const std::vector<std::string>& desktop_ids) {
  const AllowedRoots roots(data_dirs);
  for (const std::string& id : desktop_ids) {
    if (!IsPlainName(id)) {
      continue;
    }
    for (const base::FilePath& dir : data_dirs) {
      std::optional<std::string> entry = ReadRegularFile(
          dir.AppendASCII("applications").AppendASCII(id + ".desktop"), roots,
          kMaxDesktopFileBytes);
      if (!entry) {
        continue;
      }
      const std::string icon = IconOfDesktopEntry(*entry);
      if (icon.empty()) {
        continue;
      }
      if (std::optional<std::string> bytes = FindIcon(data_dirs, roots, icon)) {
        return "data:image/png;base64," + base::Base64Encode(*bytes);
      }
    }
  }
  return std::string();
}

std::vector<std::string> DesktopIdsForBrowser(std::string_view name) {
  const bool flatpak = name.ends_with(kFlatpakSuffix);
  if (flatpak) {
    name.remove_suffix(std::string_view(kFlatpakSuffix).size());
  }
  for (const KnownBrowser& browser : KnownBrowsers()) {
    if (std::find(browser.names.begin(), browser.names.end(), name) ==
        browser.names.end()) {
      continue;
    }
    if (!flatpak) {
      return browser.native_desktop_ids;
    }
    return browser.flatpak_app_id.empty()
               ? std::vector<std::string>()
               : std::vector<std::string>{browser.flatpak_app_id};
  }
  return {};
}

}  // namespace aerie_import_sources
