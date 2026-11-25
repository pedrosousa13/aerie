// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/browser_icon.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/aerie_import_sources/import_sources.h"
#include "components/aerie_import_sources/known_browsers.h"

#if BUILDFLAG(IS_POSIX)
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

std::atomic<bool> g_openat2_disabled{false};

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

// <install>/flatpak/exports/share, resolved.
bool IsFlatpakExportDir(const base::FilePath& real) {
  return real.BaseName().value() == "share" &&
         real.DirName().BaseName().value() == "exports" &&
         real.DirName().DirName().BaseName().value() == "flatpak";
}

ReadRoots MakeReadRoots(const std::vector<base::FilePath>& data_dirs) {
  ReadRoots roots;
  for (const base::FilePath& dir : data_dirs) {
    const base::FilePath real = base::MakeAbsoluteFilePath(dir);
    if (real.empty()) {
      continue;
    }
    roots.contain.push_back(real);
    roots.bases.push_back(real);
    if (IsFlatpakExportDir(real)) {
      // The exports are symlinks into <install>/app/<id>/...; allow that directory, and only
      // when it exists, so a directory that merely looks like an export directory widens nothing.
      const base::FilePath install = real.DirName().DirName();
      const base::FilePath app = base::MakeAbsoluteFilePath(install.Append("app"));
      if (!app.empty() && base::DirectoryExists(app)) {
        roots.contain.push_back(app);
        roots.bases.push_back(install);
      }
    }
  }
  return roots;
}

#if BUILDFLAG(IS_POSIX)

// Opens `real`, a path with no symlinks in it, for reading.
//
// With openat2() (Linux 5.6 and later) the open is made relative to `base_dir` with
// RESOLVE_BENEATH and RESOLVE_NO_SYMLINKS, so the kernel itself refuses the open if `real`, or a
// directory on the way to it, has been swapped for a symlink or leaves `base_dir` since it was
// resolved. Without openat2() (older kernel, or a sandbox that blocks the call) it opens `real`
// with O_NOFOLLOW, which covers only the last component: a directory on the way could still be
// swapped between the check and the open. In both cases the caller checks the opened descriptor
// with fstat.
base::ScopedFD OpenForReading(const base::FilePath& base_dir,
                              const base::FilePath& real) {
#if defined(__NR_openat2)
  if (!g_openat2_disabled.load()) {
    base::FilePath relative;
    if (base_dir.AppendRelativePath(real, &relative)) {
      base::ScopedFD dir(HANDLE_EINTR(
          open(base_dir.value().c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC)));
      if (dir.is_valid()) {
        // struct open_how of <linux/openat2.h>, declared here so an older sysroot builds.
        struct OpenHow {
          uint64_t flags;
          uint64_t mode;
          uint64_t resolve;
        } how = {O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0,
                 /*RESOLVE_NO_MAGICLINKS=*/0x02 | /*RESOLVE_NO_SYMLINKS=*/0x04 |
                     /*RESOLVE_BENEATH=*/0x08};
        long fd = HANDLE_EINTR(syscall(__NR_openat2, dir.get(),
                                       relative.value().c_str(), &how,
                                       sizeof(how)));
        if (fd >= 0) {
          return base::ScopedFD(static_cast<int>(fd));
        }
        // Refused because the path leaves the base or has a symlink in it: no icon. Only a
        // kernel or sandbox that does not offer the call falls through to the open below.
        if (errno != ENOSYS && errno != EPERM && errno != EINVAL &&
            errno != E2BIG) {
          return base::ScopedFD();
        }
      }
    }
  }
#endif
  // O_NONBLOCK so that a FIFO that slipped in cannot block the open; the caller refuses it.
  return base::ScopedFD(HANDLE_EINTR(open(
      real.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
}

#endif  // BUILDFLAG(IS_POSIX)

// The contents of a regular file of at most `max_bytes` whose resolved path lies inside
// `roots.contain`. It resolves the path once, opens the result as OpenForReading() says, and
// checks and reads through that one descriptor, so the file that was checked is the file that is
// read.
std::optional<std::string> ReadRegularFile(const base::FilePath& path,
                                           const ReadRoots& roots,
                                           size_t max_bytes) {
#if BUILDFLAG(IS_POSIX)
  const base::FilePath real = base::MakeAbsoluteFilePath(path);
  if (real.empty()) {
    return std::nullopt;
  }
  const auto inside = [&](const base::FilePath& root) {
    return root.IsParent(real);
  };
  if (std::none_of(roots.contain.begin(), roots.contain.end(), inside)) {
    return std::nullopt;
  }
  const auto base_dir =
      std::find_if(roots.bases.begin(), roots.bases.end(), inside);
  if (base_dir == roots.bases.end()) {
    return std::nullopt;
  }
  base::ScopedFD fd = OpenForReading(*base_dir, real);
  if (!fd.is_valid()) {
    return std::nullopt;
  }
  struct stat info;
  if (fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size <= 0 || static_cast<uint64_t>(info.st_size) > max_bytes) {
    return std::nullopt;
  }
  // It is a regular file: reads cannot block, and the open flag has done its job.
  const int flags = fcntl(fd.get(), F_GETFL);
  if (flags >= 0) {
    fcntl(fd.get(), F_SETFL, flags & ~O_NONBLOCK);
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
                                   const ReadRoots& roots) {
  std::optional<std::string> bytes =
      ReadRegularFile(path, roots, kMaxIconBytes);
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

}  // namespace

bool Openat2Usable() {
#if BUILDFLAG(IS_POSIX) && defined(__NR_openat2)
  if (g_openat2_disabled.load()) {
    return false;
  }
  base::ScopedFD dir(HANDLE_EINTR(open("/", O_PATH | O_DIRECTORY | O_CLOEXEC)));
  struct {
    uint64_t flags, mode, resolve;
  } how = {O_RDONLY | O_CLOEXEC, 0, 0x08 | 0x04};
  long fd = dir.is_valid()
                ? syscall(__NR_openat2, dir.get(), "etc", &how, sizeof(how))
                : -1;
  if (fd >= 0) {
    close(static_cast<int>(fd));
    return true;
  }
  return false;
#else
  return false;
#endif
}

IconResolver::IconResolver(std::vector<base::FilePath> data_dirs)
    : data_dirs_(std::move(data_dirs)), roots_(MakeReadRoots(data_dirs_)) {
  for (const base::FilePath& dir : data_dirs_) {
    themes_.push_back(ThemeNames(dir.AppendASCII("icons")));
  }
}

IconResolver::~IconResolver() = default;

std::string IconResolver::DataUrlFor(
    const std::vector<std::string>& desktop_ids) const {
  // Every place the icon file `file_name` (a plain name with ".png") may be, best first: the
  // hicolor icons of all data dirs by size, then the other themes by size, then pixmaps.
  const auto candidates = [&](const std::string& file_name) {
    std::vector<base::FilePath> paths;
    for (bool hicolor : {true, false}) {
      for (std::string_view size : kSizes) {
        for (size_t d = 0; d < data_dirs_.size(); ++d) {
          for (size_t t = 0; t < themes_[d].size(); ++t) {
            if ((t == 0) == hicolor) {  // themes_[d][0] is hicolor.
              paths.push_back(data_dirs_[d]
                                  .AppendASCII("icons")
                                  .AppendASCII(themes_[d][t])
                                  .AppendASCII(size)
                                  .AppendASCII("apps")
                                  .AppendASCII(file_name));
            }
          }
        }
      }
    }
    for (const base::FilePath& dir : data_dirs_) {
      paths.push_back(dir.AppendASCII("pixmaps").AppendASCII(file_name));
    }
    return paths;
  };

  // `icon` is an absolute path (which must lie inside the data dirs) or a plain icon name.
  const auto find_icon = [&](const std::string& icon)
      -> std::optional<std::string> {
    if (!icon.empty() && icon.front() == '/') {
      return ReadPng(base::FilePath(icon), roots_);
    }
    if (!IsPlainName(icon)) {
      return std::nullopt;
    }
    std::string stem = icon;  // The icon name may carry its extension.
    if (base::EndsWith(stem, ".png", base::CompareCase::INSENSITIVE_ASCII)) {
      stem.resize(stem.size() - 4);
    }
    for (const base::FilePath& path : candidates(stem + ".png")) {
      if (std::optional<std::string> bytes = ReadPng(path, roots_)) {
        return bytes;
      }
    }
    return std::nullopt;
  };

  for (const std::string& id : desktop_ids) {
    if (!IsPlainName(id)) {
      continue;
    }
    for (const base::FilePath& dir : data_dirs_) {
      std::optional<std::string> entry = ReadRegularFile(
          dir.AppendASCII("applications").AppendASCII(id + ".desktop"), roots_,
          kMaxDesktopFileBytes);
      if (!entry) {
        continue;
      }
      const std::string icon = IconOfDesktopEntry(*entry);
      if (icon.empty()) {
        continue;
      }
      if (std::optional<std::string> bytes = find_icon(icon)) {
        return "data:image/png;base64," + base::Base64Encode(*bytes);
      }
    }
  }
  return std::string();
}

std::string BrowserIconDataUrl(const std::vector<base::FilePath>& data_dirs,
                               const std::vector<std::string>& desktop_ids) {
  return IconResolver(data_dirs).DataUrlFor(desktop_ids);
}

void SetOpenat2DisabledForTesting(bool disabled) {
  g_openat2_disabled.store(disabled);
}

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
