// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/import_sources.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_import_sources/known_browsers.h"

namespace aerie_import_sources {

namespace {

// `path` with symlinks resolved, or `path` itself when it does not exist.
base::FilePath Canonical(const base::FilePath& path) {
  const base::FilePath resolved = base::MakeAbsoluteFilePath(path);
  return resolved.empty() ? path : resolved;
}

}  // namespace

base::FilePath HomeDirectory() {
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home;
}

std::vector<ExtraChromiumSource> ExtraChromiumSources(
    const base::FilePath& home) {
  std::vector<ExtraChromiumSource> sources;
  if (home.empty()) {
    return sources;
  }
  for (const KnownBrowser& browser : KnownBrowsers()) {
    if (browser.flatpak_app_id.empty() || browser.flatpak_data_folder.empty()) {
      continue;
    }
    sources.push_back({browser.names.front() + kFlatpakSuffix,
                       home.Append(".var/app")
                           .AppendASCII(browser.flatpak_app_id)
                           .Append("config")
                           .Append(browser.flatpak_data_folder),
                       browser.flatpak_importer_type});
  }
  // Helium keeps a Chromium profile in its own folder. Its importer type is Chrome's.
  sources.push_back({"Helium", home.Append(".config/net.imput.helium"),
                     user_data_importer::TYPE_CHROME});
  return sources;
}

void NormalizeSources(
    const base::FilePath& home,
    std::vector<user_data_importer::SourceProfile>* profiles) {
  const base::FilePath flatpak_root = Canonical(home).Append(".var/app");
  const std::u16string suffix = base::UTF8ToUTF16(std::string(kFlatpakSuffix));
  std::set<std::pair<int, base::FilePath>> seen;
  std::vector<user_data_importer::SourceProfile> kept;
  for (auto& profile : *profiles) {
    if (!profile.source_path.empty()) {
      const base::FilePath folder = Canonical(profile.source_path);
      if (!seen.insert({static_cast<int>(profile.importer_type), folder})
               .second) {
        continue;
      }
      if (flatpak_root.IsParent(folder) &&
          !profile.importer_name.ends_with(suffix)) {
        profile.importer_name += suffix;
      }
    }
    kept.push_back(std::move(profile));
  }
  *profiles = std::move(kept);
}

}  // namespace aerie_import_sources
