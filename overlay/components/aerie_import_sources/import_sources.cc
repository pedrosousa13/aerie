// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/import_sources.h"

#include <set>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"

namespace aerie_import_sources {

namespace {

struct FlatpakChromium {
  const char* brand;
  const char* app_id;
  const char* data_folder;  // Under <home>/.var/app/<app id>/config.
  user_data_importer::ImporterType type;
};

constexpr FlatpakChromium kFlatpakChromium[] = {
    {"Google Chrome (Flatpak)", "com.google.Chrome", "google-chrome",
     user_data_importer::TYPE_CHROME},
    {"Chromium (Flatpak)", "org.chromium.Chromium", "chromium",
     user_data_importer::TYPE_CHROME},
    {"Brave (Flatpak)", "com.brave.Browser", "BraveSoftware/Brave-Browser",
     user_data_importer::TYPE_BRAVE},
    {"Microsoft Edge (Flatpak)", "com.microsoft.Edge", "microsoft-edge",
     user_data_importer::TYPE_EDGE_CHROMIUM},
    {"Vivaldi (Flatpak)", "com.vivaldi.Vivaldi", "vivaldi",
     user_data_importer::TYPE_VIVALDI},
    {"Opera (Flatpak)", "com.opera.Opera", "opera",
     user_data_importer::TYPE_OPERA},
};

// `path` with symlinks resolved, or `path` itself when it does not exist.
base::FilePath Canonical(const base::FilePath& path) {
  const base::FilePath resolved = base::MakeAbsoluteFilePath(path);
  return resolved.empty() ? path : resolved;
}

}  // namespace

std::vector<ExtraChromiumSource> ExtraChromiumSources(
    const base::FilePath& home) {
  std::vector<ExtraChromiumSource> sources;
  if (home.empty()) {
    return sources;
  }
  for (const FlatpakChromium& entry : kFlatpakChromium) {
    sources.push_back(
        {entry.brand,
         home.Append(".var/app")
             .AppendASCII(entry.app_id)
             .Append("config")
             .Append(entry.data_folder),
         entry.type});
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
  std::set<std::pair<int, base::FilePath>> seen;
  std::vector<user_data_importer::SourceProfile> kept;
  for (auto& profile : *profiles) {
    if (!profile.source_path.empty()) {
      const base::FilePath folder = Canonical(profile.source_path);
      if (!seen.insert({static_cast<int>(profile.importer_type), folder})
               .second) {
        continue;
      }
      const std::u16string suffix = u" (Flatpak)";  // As kFlatpakSuffix.
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
