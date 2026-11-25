// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_IMPORT_SOURCES_IMPORT_SOURCES_H_
#define COMPONENTS_AERIE_IMPORT_SOURCES_IMPORT_SOURCES_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_type.h"

namespace aerie_import_sources {

// The suffix that tells the user a row is a Flatpak install.
inline constexpr char kFlatpakSuffix[] = " (Flatpak)";

// The user's home directory, or an empty path when it is not known. The importer list hooks
// (patches/onboarding/0002) take it from here, so they share one way to find it.
base::FilePath HomeDirectory();

// A Chromium-format user data folder that Chromium's own importer list does not look at.
struct ExtraChromiumSource {
  std::string brand;  // The name of the row, such as "Chromium (Flatpak)".
  base::FilePath user_data_folder;
  user_data_importer::ImporterType importer_type;
};

// Flatpak installs of Chromium-family browsers (their data lives in
// <home>/.var/app/<id>/config) and Helium (<home>/.config/net.imput.helium). Helium has no
// importer type of its own: its profiles have Chromium's format, so they import as Chrome.
// The folders are not checked here; the importer list skips one that holds no profile.
std::vector<ExtraChromiumSource> ExtraChromiumSources(
    const base::FilePath& home);

// Makes the list one row per install of a browser profile. A Flatpak install (a source
// under <home>/.var/app/<id>/) gets " (Flatpak)" at the end of its name, so it reads
// apart from the native install of the same browser. A source that is listed twice (the
// same importer type and the same folder once symlinks are resolved) is listed once, the
// first time. Rows without a folder (a bookmarks file) are kept as they are. The order
// of the rest does not change.
void NormalizeSources(const base::FilePath& home,
                      std::vector<user_data_importer::SourceProfile>* profiles);

}  // namespace aerie_import_sources

#endif  // COMPONENTS_AERIE_IMPORT_SOURCES_IMPORT_SOURCES_H_
