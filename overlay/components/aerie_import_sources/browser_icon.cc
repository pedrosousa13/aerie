// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/browser_icon.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "base/base64.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/aerie_import_sources/import_sources.h"

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

// The bytes of `path` when it is a regular PNG file of at most kMaxIconBytes.
bool ReadPng(const base::FilePath& path, std::string* bytes) {
  base::File::Info info;
  if (!base::GetFileInfo(path, &info) || info.is_directory ||
      info.size <= 0 || info.size > static_cast<int64_t>(kMaxIconBytes)) {
    return false;
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents, kMaxIconBytes) ||
      contents.size() < 16) {
    return false;
  }
  const std::string_view view(contents);
  if (!view.starts_with(kPngMagic) || view.substr(12, 4) != kIhdr) {
    return false;
  }
  *bytes = std::move(contents);
  return true;
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

// Looks `icon` up in the icon directories of all `data_dirs`. `icon` is an absolute path or
// a plain icon name.
bool FindIcon(const std::vector<base::FilePath>& data_dirs,
              const std::string& icon,
              std::string* bytes) {
  if (!icon.empty() && icon.front() == '/') {
    return ReadPng(base::FilePath(icon), bytes);
  }
  if (!IsPlainName(icon)) {
    return false;
  }
  // The icon name may carry its extension.
  std::string stem = icon;
  if (base::EndsWith(stem, ".png", base::CompareCase::INSENSITIVE_ASCII)) {
    stem.resize(stem.size() - 4);
  }
  const std::string file = stem + ".png";

  std::vector<std::vector<std::string>> themes;
  for (const auto& dir : data_dirs) {
    themes.push_back(ThemeNames(dir.AppendASCII("icons")));
  }
  // hicolor of every data dir first, so a Flatpak export beats a stray theme.
  for (int pass = 0; pass < 2; ++pass) {
    for (std::string_view size : kSizes) {
      for (size_t d = 0; d < data_dirs.size(); ++d) {
        const auto& names = themes[d];
        for (size_t t = 0; t < names.size(); ++t) {
          if ((pass == 0) != (t == 0)) {
            continue;  // Pass 0 is hicolor (always first), pass 1 the rest.
          }
          const base::FilePath path = data_dirs[d]
                                          .AppendASCII("icons")
                                          .AppendASCII(names[t])
                                          .AppendASCII(size)
                                          .AppendASCII("apps")
                                          .AppendASCII(file);
          if (ReadPng(path, bytes)) {
            return true;
          }
        }
      }
    }
  }
  for (const auto& dir : data_dirs) {
    if (ReadPng(dir.AppendASCII("pixmaps").AppendASCII(file), bytes)) {
      return true;
    }
  }
  return false;
}

struct BrowserIds {
  std::string_view name;
  std::vector<std::string> native;
  std::vector<std::string> flatpak;
};

const std::vector<BrowserIds>& Table() {
  static const std::vector<BrowserIds> table = {
      {"Google Chrome", {"google-chrome", "google-chrome-stable"}, {"com.google.Chrome"}},
      {"Google Chrome Beta", {"google-chrome-beta"}, {}},
      {"Google Chrome Dev", {"google-chrome-unstable"}, {}},
      {"Chromium", {"chromium", "chromium-browser"}, {"org.chromium.Chromium"}},
      {"Brave", {"brave-browser", "brave"}, {"com.brave.Browser"}},
      {"Microsoft Edge", {"microsoft-edge", "microsoft-edge-stable"}, {"com.microsoft.Edge"}},
      {"Vivaldi", {"vivaldi-stable", "vivaldi"}, {"com.vivaldi.Vivaldi"}},
      {"Opera", {"opera"}, {"com.opera.Opera"}},
      {"Yandex", {"yandex-browser", "yandex-browser-beta"}, {}},
      {"NAVER Whale", {"naver-whale", "naver-whale-stable"}, {}},
      {"Helium", {"helium", "net.imput.helium"}, {}},
      {"Firefox", {"firefox", "firefox-esr", "org.mozilla.firefox"}, {"org.mozilla.firefox"}},
      {"Firefox Developer Edition", {"firefox-developer-edition"}, {}},
      {"Firefox Nightly", {"firefox-nightly"}, {}},
      {"Zen", {"zen", "zen-browser", "app.zen_browser.zen"}, {"app.zen_browser.zen"}},
  };
  return table;
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
  for (const std::string& id : desktop_ids) {
    if (!IsPlainName(id)) {
      continue;
    }
    for (const auto& dir : data_dirs) {
      std::string entry;
      if (!base::ReadFileToStringWithMaxSize(
              dir.AppendASCII("applications").AppendASCII(id + ".desktop"),
              &entry, kMaxDesktopFileBytes)) {
        continue;
      }
      const std::string icon = IconOfDesktopEntry(entry);
      std::string bytes;
      if (!icon.empty() && FindIcon(data_dirs, icon, &bytes)) {
        return "data:image/png;base64," + base::Base64Encode(bytes);
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
  for (const BrowserIds& row : Table()) {
    if (row.name == name) {
      return flatpak ? row.flatpak : row.native;
    }
  }
  return {};
}

}  // namespace aerie_import_sources
